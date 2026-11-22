#!/usr/bin/env python3
"""Exercise the public TLS and ACME HTTPS APIs against local TLS peers."""

import argparse
import concurrent.futures
import pathlib
import socket
import ssl
import subprocess
import tempfile
import time


CA_REJECTIONS = {"untrusted", "wrong-name"}


def receive_exact(connection, size):
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            break
        data.extend(chunk)
    return bytes(data)


def receive_head(connection):
    data = bytearray()
    while not data.endswith(b"\r\n\r\n"):
        chunk = connection.recv(1)
        if not chunk or len(data) >= 16384:
            raise AssertionError("incomplete or oversized HTTP request head")
        data.extend(chunk)
    return bytes(data)


def serve(listener, context, case):
    with listener.accept()[0] as accepted:
        accepted.settimeout(5)
        if case == "handshake-timeout":
            time.sleep(1.1)
            return
        try:
            connection = context.wrap_socket(accepted, server_side=True)
        except ssl.SSLError:
            if case in CA_REJECTIONS:
                return
            raise
        with connection:
            if case in CA_REJECTIONS:
                raise AssertionError("an unverified peer completed a handshake")
            if case in {"chunked-response", "truncated-http-body", "absolute-http-deadline"}:
                head = receive_head(connection)
                assert head.startswith(b"GET /directory?name=test HTTP/1.1\r\n"), head
                assert b"Host: 127.0.0.1:" in head, head
                if case == "absolute-http-deadline":
                    # Each wait is below the request's 400 ms deadline, but their sum exceeds it.
                    time.sleep(0.22)
                    connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n")
                    time.sleep(0.27)
                    try:
                        connection.sendall(b"hello")
                    except (ConnectionError, ssl.SSLError):
                        pass
                    return
                if case == "chunked-response":
                    connection.sendall(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                                       b"2\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n")
                else:
                    connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc")
            else:
                got = receive_exact(connection, 4)
                assert got == b"ping", got
                if case == "round-trip":
                    connection.sendall(b"pong")
                elif case == "truncated-record":
                    with socket.socket(fileno=connection.detach()) as raw:
                        raw.sendall(b"\x17\x03\x03\x00\x20abc")
                    return
            try:
                connection.unwrap().close()
            except (ConnectionError, ssl.SSLError):
                # A client may already have closed after a complete response.
                if case == "orderly-close":
                    raise


def run(args):
    result = subprocess.run(args, text=True, capture_output=True, timeout=30)
    if result.returncode:
        raise AssertionError(f"command exited with {result.returncode}: {result.stderr}\n{result.stdout}")
    return result.stdout.strip()


def buffered_server(runner, openssl, certificate, key, client_certificate, client_key):
    server_args = runner + ["--", "server", str(certificate), str(key), str(client_certificate)]
    for authenticated in (True, False):
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            with subprocess.Popen(server_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as server:
                try:
                    address = pool.submit(server.stdout.readline).result(timeout=8).strip()
                    if not address.startswith("127.0.0.1:"):
                        output, error = server.communicate(timeout=8)
                        raise AssertionError(f"invalid Neura server address: {address!r}\n{output}\n{error}")
                    client_args = [str(openssl), "s_client", "-quiet", "-tls1_3", "-connect", address, "-servername",
                                   "localhost", "-verify_hostname", "localhost", "-verify_return_error", "-CAfile",
                                   str(certificate)]
                    if authenticated:
                        client_args.extend(["-cert", str(client_certificate), "-key", str(client_key)])
                    client = subprocess.run(client_args, text=True, input="ping", capture_output=True, timeout=10)
                    server_out, server_err = server.communicate(timeout=8)
                    assert server.returncode == 0, f"Neura server failed: {server_err}"
                    if authenticated:
                        assert client.returncode == 0 and client.stdout == "pong", (client.stdout, client.stderr)
                        assert server_out.strip() == "ping", (server_out, server_err)
                        print("PASS buffered-server-mutual-tls13")
                    else:
                        assert client.returncode != 0 and not client.stdout, (client.stdout, client.stderr)
                        assert server_out.strip() == "PermissionDenied", (server_out, server_err)
                        assert "alert certificate required" in client.stderr.lower(), client.stderr
                        print("PASS buffered-server-missing-client-certificate")
                finally:
                    if server.poll() is None:
                        server.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neura", type=pathlib.Path, required=True)
    parser.add_argument("--openssl", type=pathlib.Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="neura-tls-loopback-") as name:
        root = pathlib.Path(name)
        certificate = root / "certificate.pem"
        key = root / "key.pem"
        run([str(args.openssl), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
             "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
             "-keyout", str(key), "-out", str(certificate)])
        client_certificate = root / "client-certificate.pem"
        client_key = root / "client-key.pem"
        run([str(args.openssl), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
             "-subj", "/CN=neura-loopback-client", "-addext", "extendedKeyUsage=clientAuth", "-keyout",
             str(client_key), "-out", str(client_certificate)])
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        context.maximum_version = ssl.TLSVersion.TLSv1_3
        context.set_alpn_protocols(["http/1.1"])
        context.load_cert_chain(certificate, key)
        image = root / "client.nvr"
        source = pathlib.Path(__file__).with_name("loopback") / "client.nra"
        run([str(args.neura), "build", str(source), "-o", str(image)])
        runner = [str(args.neura), "run", str(image)]
        buffered_server(runner, args.openssl, certificate, key, client_certificate, client_key)
        trace = root / "tls.trace"
        dtls = run(runner + ["--", "dtls", "127.0.0.1:9"])
        assert dtls == "Unsupported", f"expected typed DTLS 1.3 Unsupported, got {dtls!r}"
        print("PASS unsupported-dtls13")
        cases = {"round-trip": "pong", "orderly-close": "end", "untrusted": "PermissionDenied",
                 "wrong-name": "PermissionDenied", "handshake-timeout": "TimedOut", "truncated-record": "ConnectionReset",
                 "chunked-response": "200:hello", "truncated-http-body": "acme.transport",
                 "absolute-http-deadline": "TimedOut", "refused": "ConnectionRefused"}
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for case, expected in cases.items():
                with socket.socket() as listener:
                    listener.settimeout(5)
                    listener.bind(("127.0.0.1", 0))
                    address = f"127.0.0.1:{listener.getsockname()[1]}"
                    if case == "absolute-http-deadline":
                        mode = "https-deadline"
                    elif case in {"chunked-response", "truncated-http-body"}:
                        mode = "https"
                    else:
                        mode = "tls"
                    identity = "wrong.test" if case == "wrong-name" else "localhost"
                    arguments = ["--", mode, address, "" if case == "untrusted" else str(certificate), identity]
                    if case == "refused":
                        actual = run(runner + arguments)
                    else:
                        listener.listen(1)
                        peer = pool.submit(serve, listener, context, case)
                        options = ["--record", str(trace)] if case == "round-trip" else []
                        actual = run(runner + options + arguments)
                        peer.result(timeout=8)
                    assert actual == expected, f"{case}: expected {expected!r}, got {actual!r}"
                    print(f"PASS {case}")
                    if case == "round-trip":
                        replay = run(runner + ["--replay", str(trace)] + arguments)
                        assert replay == expected, f"replay differs: {replay!r}"
                        print("PASS round-trip replay")


if __name__ == "__main__":
    main()
