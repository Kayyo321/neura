import argparse
from contextlib import contextmanager
from pathlib import Path
import select
import shutil
import signal
import socket
import subprocess
import tempfile
import time


HOST = "127.0.0.1"
TIMEOUT = 4
PLAIN = (0, "-", "-")


def command(fixture, cap, endpoints):
    return [fixture, str(cap), *(str(item) for endpoint in endpoints for item in endpoint)]


def line_from(process):
    readable, _, _ = select.select([process.stdout], [], [], TIMEOUT)
    if not readable:
        raise RuntimeError("timed out reading the foreground responder")
    line = process.stdout.readline().strip()
    if not line:
        raise RuntimeError(f"foreground responder exited: {process.stderr.read().strip()}")
    return line


def no_listener(port):
    with socket.socket() as probe:
        probe.settimeout(TIMEOUT)
        if probe.connect_ex((HOST, port)) == 0:
            raise AssertionError(f"a listener remained open on port {port}")


@contextmanager
def foreground(fixture, cap, endpoints):
    with subprocess.Popen(command(fixture, cap, endpoints), stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True) as process:
        ports = []
        try:
            ready = line_from(process).split()
            assert ready[0] == "READY" and len(ready) == len(endpoints) + 1, ready
            ports = list(map(int, ready[1:]))
            assert len(set(ports)) == len(ports) and all(ports), ports
            yield process, ports
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            try:
                process.wait(TIMEOUT)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(TIMEOUT)
                raise AssertionError("foreground responder did not stop on SIGTERM")
            finally:
                for port in ports:
                    no_listener(port)
            assert process.returncode == 0, process.stderr.read()


def recv_exact(client, size):
    received = bytearray()
    while len(received) < size:
        part = client.recv(size - len(received))
        if not part:
            raise EOFError("connection closed before the full reply")
        received.extend(part)
    return bytes(received)


def request(client, close=False):
    connection = b"Connection: close\r\n" if close else b""
    client.sendall(b"GET /parts?q=bolt HTTP/1.1\r\nHost: localhost\r\n" + connection + b"\r\n")
    reply = b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n" + connection + b"\r\nfixed"
    assert recv_exact(client, len(reply)) == reply
    if close:
        assert client.recv(1) == b"", "server did not close after its final reply"


def connect(port):
    return socket.create_connection((HOST, port), timeout=TIMEOUT)


def available_port():
    with socket.socket() as probe:
        probe.bind((HOST, 0))
        return probe.getsockname()[1]


def fail_start(fixture, endpoints, diagnostic):
    result = subprocess.run(command(fixture, 2, endpoints), capture_output=True, text=True, timeout=TIMEOUT)
    assert result.returncode != 0 and not result.stdout and diagnostic in result.stderr, result


def transactional_bind(fixture):
    with socket.socket() as occupied:
        occupied.bind((HOST, 0))
        occupied.listen()
        port = available_port()
        fail_start(fixture, ((port, "-", "-"), (occupied.getsockname()[1], "-", "-")), "bind HTTP listener")
        no_listener(port)
        with foreground(fixture, 2, ((port, "-", "-"),)):
            with connect(port) as client:
                request(client, close=True)


def connection_limit(fixture):
    with foreground(fixture, 1, (PLAIN, PLAIN)) as (_, (first, second)):
        with connect(first) as active:
            request(active)
            with connect(second) as refused:
                assert refused.recv(1) == b"", "over-limit connection reached the responder"
            request(active, close=True)
        # The active slot is released when the exchange returns and the
        # descriptor closes, which can follow receipt of the final reply.
        deadline = time.monotonic() + TIMEOUT
        while True:
            try:
                with connect(second) as recovered:
                    request(recovered, close=True)
                break
            except (EOFError, ConnectionResetError, BrokenPipeError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.02)


def make_key_pair(openssl, directory, label):
    certificate = directory / f"{label}.pem"
    key = directory / f"{label}.key"
    subprocess.run([openssl, "req", "-x509", "-noenc", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                    "-sha256", "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                    "-out", certificate, "-keyout", key], capture_output=True, check=True, timeout=TIMEOUT)
    return certificate, key


def replace_file(source, target):
    draft = target.with_suffix(".new")
    shutil.copyfile(source, draft)
    draft.replace(target)


def replace_pair(source, target):
    for original, destination in zip(source, target):
        replace_file(original, destination)


@contextmanager
def secure_client(port, certificate, version):
    import ssl

    context = ssl.create_default_context(cafile=str(certificate))
    context.minimum_version = context.maximum_version = version
    with connect(port) as raw:
        with context.wrap_socket(raw, server_hostname="localhost", suppress_ragged_eofs=False) as tls:
            yield tls


def tls_reload(fixture, openssl):
    import ssl

    with tempfile.TemporaryDirectory(prefix="neura-http-listener-") as path:
        directory = Path(path)
        old_a = make_key_pair(openssl, directory, "old-a")
        old_b = make_key_pair(openssl, directory, "old-b")
        new_a = make_key_pair(openssl, directory, "new-a")
        new_b = make_key_pair(openssl, directory, "new-b")
        active_a = directory / "active-a.pem", directory / "active-a.key"
        active_b = directory / "active-b.pem", directory / "active-b.key"
        replace_pair(old_a, active_a)
        replace_pair(old_b, active_b)

        port = available_port()
        fail_start(fixture, ((port, "-", "-"), (0, old_a[0], directory / "missing.key")), "private key")
        no_listener(port)
        fail_start(fixture, ((port, "-", "-"), (0, old_a[0], old_b[1])), "private key")
        no_listener(port)

        v12 = ssl.TLSVersion.TLSv1_2
        v13 = ssl.TLSVersion.TLSv1_3
        with foreground(fixture, 8, (PLAIN, (0, *active_a), (0, *active_b))) as (process, (plain, tls_a, tls_b)):
            with secure_client(tls_a, old_a[0], v13) as existing:
                request(existing)
                with connect(plain) as client:
                    request(client, close=True)
                with secure_client(tls_b, old_b[0], v12) as client:
                    request(client, close=True)

                replace_pair(new_a, active_a)
                replace_file(new_b[0], active_b[0])
                process.send_signal(signal.SIGHUP)
                assert line_from(process) == "RELOAD_FAILED"
                with secure_client(tls_a, old_a[0], v13) as client:
                    request(client, close=True)
                with secure_client(tls_b, old_b[0], v12) as client:
                    request(client, close=True)

                replace_file(new_b[1], active_b[1])
                process.send_signal(signal.SIGHUP)
                assert line_from(process) == "RELOADED"
                with secure_client(tls_a, new_a[0], v13) as client:
                    request(client, close=True)
                with secure_client(tls_b, new_b[0], v12) as client:
                    request(client, close=True)
                request(existing, close=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture")
    parser.add_argument("--openssl")
    args = parser.parse_args()
    subprocess.run([args.fixture, "--ownership"], check=True, timeout=TIMEOUT)
    transactional_bind(args.fixture)
    connection_limit(args.fixture)
    if args.openssl:
        tls_reload(args.fixture, args.openssl)
    else:
        fail_start(args.fixture, (PLAIN, (0, "certificate.pem", "private.key")), "no TLS adapter")


if __name__ == "__main__":
    main()
