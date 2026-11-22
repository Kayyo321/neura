#!/usr/bin/env python3
"""Public HTTP/2 frame output over real, verified TLS on private loopback."""

import argparse
import concurrent.futures
import pathlib
import re
import socket
import ssl
import struct
import subprocess
import tempfile
import threading


def run(command, timeout=30):
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
    return result.stdout


def receive_exact(connection, size):
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        assert chunk, ("truncated peer data", size, len(data))
        data.extend(chunk)
    return bytes(data)


def frame(connection):
    head = receive_exact(connection, 9)
    size = int.from_bytes(head[:3], "big")
    assert size <= 16384, size
    return head[3], head[4], int.from_bytes(head[5:], "big"), receive_exact(connection, size)


def serve(listener, context, case, release):
    with listener.accept()[0] as accepted:
        accepted.settimeout(5)
        accepted.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        if case in {"handshake-timeout", "cancel-connect"}:
            assert release.wait(10), "harness did not release silent peer"
            return
        try:
            connection = context.wrap_socket(accepted, server_side=True)
        except ssl.SSLError:
            if case in {"wrong-hostname", "untrusted", "default-trust"}:
                return
            raise
        with connection:
            if case in {"wrong-hostname", "untrusted", "default-trust"}:
                raise AssertionError("unverified connection completed")
            if case == "wrong-alpn":
                assert connection.recv(1) == b"", "preface sent without h2 ALPN"
                return
            assert connection.selected_alpn_protocol() == "h2"
            assert receive_exact(connection, 24) == b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
            assert frame(connection) == (4, 0, 0, struct.pack(">HIHI", 2, 0, 1, 0))
            if case == "blocked-write":
                # The harness releases us only after the bounded client write
                # has failed, so an early close cannot impersonate a timeout.
                assert release.wait(20), "harness did not release non-reading peer"
                return
            connection.sendall(b"\0\0\0\x04\0\0\0\0\0")
            if case in {"close-timeout", "cancel-close"}:
                assert frame(connection) == (7, 0, 0, b"\0" * 8)
                assert release.wait(10), "harness did not release non-closing peer"
                return
            assert frame(connection) == (6, 0, 0, b"12345678")
            assert frame(connection) == (250, 0, 0, b"abc")
            assert frame(connection) == (7, 0, 0, b"\0" * 8)
            connection.unwrap().close()


def assert_heartbeat_during_wait(trace, service, error):
    requests = []
    completions = {}
    lines = trace.read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        if line.startswith("request "):
            request = re.fullmatch(r"request (\S+) \d+ (.*) task (\d+) tick (\d+)(?: (\w+))?", line)
            assert request, line
            name, arguments, task, issued, overlap = request.groups()
            requests.append((name, arguments, task, int(issued), overlap, lines[index + 1]))
        elif line.startswith("deliver "):
            _, tick, request_id, *flags = line.split()
            completions[int(request_id)] = (int(tick), flags)
    stalled = [(i, request) for i, request in enumerate(requests)
               if request[0] == "neura.net.tls." + service and request[-1] == f"error {error}"]
    assert len(stalled) == 1, (service, error, [(r[0], r[-1]) for r in requests if r[-1].startswith("error")])
    index, (_, _, task, issued, overlap, _) = stalled[0]
    assert overlap == "cancellable", (service, overlap)
    delivered, flags = completions[index]
    assert flags == (["cancelled"] if error == 15 else []), (service, flags)
    assert any(name == "neura.console.write_line" and args == '["heartbeat"]' and other != task
               and issued < tick < delivered for name, args, other, tick, _, _ in requests), (
        service, task, issued, delivered, [request[:4] for request in requests if "console" in request[0]])


def check_overlap(args, root, cert, key):
    image = root / "h2-overlap.nvr"
    source = pathlib.Path(__file__).with_name("loopback") / "h2-overlap-client.nra"
    run([str(args.compiler or args.neura), "build", str(source), "-o", str(image)])
    runner = [str(args.neura), "run", str(image)]
    scenarios = [
        ("handshake-timeout", "connect_overlapped", "TimedOut", 12),
        ("blocked-write", "write_overlapped", "TimedOut", 12),
        ("close-timeout", "read_overlapped", "TimedOut", 12),
        ("cancel-connect", "connect_overlapped", "Cancelled", 15),
        ("cancel-close", "read_overlapped", "Cancelled", 15),
    ]
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.set_alpn_protocols(["h2"])
    context.load_cert_chain(cert, key)
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        for case, service, expected, error in scenarios:
            trace = root / f"{case}.trace"
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                listener.settimeout(5)
                listener.listen(1)
                release = threading.Event()
                future = pool.submit(serve, listener, context, case, release)
                target = f"127.0.0.1:{listener.getsockname()[1]}"
                arguments = ["--", case, target, str(cert)]
                try:
                    actual = run(runner + ["--record", str(trace)] + arguments, timeout=25)
                finally:
                    release.set()
                future.result(timeout=5)
            assert [line for line in actual.splitlines() if line != "heartbeat"] == [expected], (case, actual)
            assert_heartbeat_during_wait(trace, service, error)
            print(f"PASS h2-{case}-heartbeat", flush=True)
            if case in {"handshake-timeout", "cancel-connect"}:
                assert run(runner + ["--replay", str(trace)] + arguments) == actual
                print(f"PASS h2-{case}-replay", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neura", type=pathlib.Path, required=True)
    parser.add_argument("--compiler", type=pathlib.Path,
                        help="optional isolated neurac used to compile the worker's stdlib")
    parser.add_argument("--openssl", type=pathlib.Path, required=True)
    parser.add_argument("--hpack", action="store_true", help="also verify pure codec recording with all capabilities denied")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="neura-h2-loopback-") as directory:
        root = pathlib.Path(directory)
        cert, key = root / "server.pem", root / "server.key"
        run([str(args.openssl), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
             "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
             "-keyout", str(key), "-out", str(cert)])
        image = root / "h2.nvr"
        source = pathlib.Path(__file__).with_name("loopback") / "h2-client.nra"
        run([str(args.compiler or args.neura), "build", str(source), "-o", str(image)])
        runner = [str(args.neura), "run", str(image)]
        cases = {"round-trip": "ok", "wrong-hostname": "PermissionDenied", "untrusted": "PermissionDenied",
                 "default-trust": "PermissionDenied", "wrong-alpn": "h2.alpn", "handshake-timeout": "TimedOut",
                 "blocked-write": "TimedOut", "close-timeout": "TimedOut", "refused": "ConnectionRefused"}
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for case, expected in cases.items():
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = ssl.TLSVersion.TLSv1_2
                context.set_alpn_protocols(["http/1.1" if case == "wrong-alpn" else "h2"])
                context.load_cert_chain(cert, key)
                trace = root / "h2.trace"
                with socket.socket() as listener:
                    listener.bind(("127.0.0.1", 0))
                    listener.settimeout(5)
                    address = f"127.0.0.1:{listener.getsockname()[1]}"
                    name = "wrong.test" if case == "wrong-hostname" else "localhost"
                    arguments = ["--", case, address, "" if case == "untrusted" else str(cert), name]
                    options = ["--record", str(trace)] if case == "round-trip" else []
                    if case == "refused":
                        actual = run(runner + arguments)
                    else:
                        listener.listen(1)
                        release = threading.Event()
                        future = pool.submit(serve, listener, context, case, release)
                        try:
                            actual = run(runner + options + arguments, timeout=20)
                        finally:
                            release.set()
                        future.result(timeout=5)
                    assert actual == expected + "\n", (case, actual, expected)
                print(f"PASS h2-{case}", flush=True)
                if case == "round-trip":
                    replayed = run(runner + ["--replay", str(trace)] + arguments)
                    assert replayed == actual, (replayed, actual)
                    print("PASS h2-round-trip-record-replay", flush=True)
            denied = run(runner + ["--deny-net", "--", "round-trip", "127.0.0.1:9", str(cert), "localhost"])
            assert denied == "CapabilityDenied\n", denied
            print("PASS h2-denied-net", flush=True)
        if args.hpack:
            trace = root / "hpack.trace"
            codec = ["--deny-all", "--", "hpack"]
            assert run(runner + ["--record", str(trace)] + codec) == "ok\n"
            assert run(runner + ["--replay", str(trace)] + codec) == "ok\n"
            print("PASS hpack-denied-all-record-replay", flush=True)
        check_overlap(args, root, cert, key)


if __name__ == "__main__":
    main()
