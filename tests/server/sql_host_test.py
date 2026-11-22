#!/usr/bin/env python3
"""End-to-end tests of Std.Data.Sql under neura-server-host (docs/runtime/Sql.md).

SQLite (always): the CRUD example examples/server/parts-sql.nra served by the
host and driven with curl when it is available, the request leases
(rollback and return of a connection a request leaves open, a statement
interrupted at the request deadline), secrets kept from workers,
migrations (E1044, manual and on_start, `migrate`, a changed checksum),
record and replay without a database, and a missing pool failing the
startup check.

PostgreSQL (--postgres): the same CRUD example and probes against servers 16,
17 and 18 in Docker with host networking on 127.0.0.1, the PostgreSQL test
file under `neura test`, cancellation of a running statement, and the
connection string rules. Exits 77 (skipped) when Docker or the PostgreSQL
build is missing.
"""

import argparse
import http.client
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

FAILURES = []
SKIP = 77
# Windows (docs/design/Windows030.md, W34): the host runs in a process group
# of its own, and Ctrl+Break drains it as SIGTERM does elsewhere (terminate()
# would end it with TerminateProcess, without its drain).
WINDOWS = os.name == "nt"
NEW_GROUP = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if WINDOWS else {}

PG_IMAGES = {
    "16.16": "postgres@sha256:81e77a4833487b8132fe4b00bedd39dc3f74b8641e387f19312a48f7de218c2e",
    "17.12": "postgres@sha256:a77aa70e641402a21411b92b721c58d10f23b50ae5fdd2333efd22d166e1e5f7",
    "18.7": "postgres@sha256:b6972e1115a4ff8eba5d7f7d19184926d1e16d7d1136ae1e1a7ebf286da0f015",
}
PG_PASSWORD = "neura-sql-test-secret"
# The probe's request deadline (S-8): short, so /slow ends in a 504 soon, but
# long enough for /big?1100000 to build its 1.1 MB parameter, which takes
# seconds without the JIT on a slow runner (a 504 on x86_64 macOS, release
# trial 16). NEURA_TEST_TIMEOUT_SCALE comes from CMake (docs/ContinuousIntegration.md).
TIMEOUT_SCALE = int(os.environ.get("NEURA_TEST_TIMEOUT_SCALE", "1"))
# At most 10 s: /slow's answer must come well inside call()'s 30 s.
PROBE_DEADLINE = min(2 * TIMEOUT_SCALE, 10)


def check(condition, message):
    if not condition:
        FAILURES.append(message)
        print("FAIL:", message, flush=True)


class Host:
    def __init__(self, args, image, *options, expect_ready=True, env=None, listen=True):
        self.err_path = Path(tempfile.mkstemp(prefix="sql-host-", suffix=".err", dir=args.work)[1])
        self.err = open(self.err_path, "w")
        command = [args.host]
        if listen:
            command += ["--listen", "127.0.0.1:0"]
        command += [*options, str(image)]
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=self.err, text=True, env=env,
                                        **NEW_GROUP)
        self.port = None
        self.ready = False
        deadline = time.time() + 60
        while time.time() < deadline:
            line = self.process.stdout.readline()
            if not line:
                break
            if "listening on" in line:
                self.port = int(line.strip().rsplit(":", 1)[1])
            if line.strip().endswith("ready"):
                self.ready = True
                break
        if expect_ready and not self.ready:
            self.stop()
            raise RuntimeError("the host did not become ready:\n" + self.stderr())

    def stderr(self):
        if not self.err.closed:
            self.err.flush()
        return self.err_path.read_text()

    def stop(self):
        if self.process.poll() is None:
            if WINDOWS:
                self.process.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                self.process.terminate()
            try:
                self.process.wait(20)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if not self.err.closed:
            self.err.close()
        return self.process.returncode


def run_host(args, image, *options, env=None, timeout=120):
    """Runs the host to completion (a refused start or `migrate`)."""
    return subprocess.run([args.host, *options, str(image)], capture_output=True, text=True, env=env,
                          timeout=timeout)


def call(args, port, method, path, body=None, timeout=30):
    """One request: with curl when the build found it, otherwise with http.client."""
    if args.curl:
        command = [args.curl, "-s", "-S", "--max-time", str(timeout), "-X", method, "-o", "-", "-w",
                   "\n%{http_code}", f"http://127.0.0.1:{port}{path}"]
        if body is not None:
            command += ["--data-binary", body]
        result = subprocess.run(command, capture_output=True, timeout=timeout + 10)
        text, _, code = result.stdout.rpartition(b"\n")
        return int(code or b"0"), text
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    connection.request(method, path, body=body)
    response = connection.getresponse()
    data = response.read()
    connection.close()
    return response.status, data


def build(args, source, name):
    image = Path(args.work) / name
    result = subprocess.run([args.neura, "build", str(Path(args.source) / source), "-o", str(image)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"cannot build {source}:\n{result.stdout}{result.stderr}")
    return image


# ---------------------------------------------------------------------------
# Shared journeys


def crud_journey(args, port, label):
    status, body = call(args, port, "POST", "/parts", b'{"code":"bolt","quantity":5}')
    check(status == 201 and json.loads(body)["code"] == "bolt", f"{label} create: {status} {body!r}")
    first = json.loads(body)["id"] if status == 201 else 0
    status, body = call(args, port, "POST", "/parts", b'{"code":"nut","quantity":7,"note":"m6"}')
    check(status == 201, f"{label} second create: {status} {body!r}")
    second = json.loads(body)["id"] if status == 201 else 0
    status, body = call(args, port, "POST", "/parts", b'{"code":"bolt","quantity":1}')
    check(status == 409, f"{label} duplicate code: {status} {body!r}")
    status, body = call(args, port, "GET", "/parts")
    listed = json.loads(body) if status == 200 else []
    check(status == 200 and [p["code"] for p in listed] == ["bolt", "nut"], f"{label} list: {status} {body!r}")
    status, body = call(args, port, "GET", f"/parts/{second}")
    check(status == 200 and json.loads(body)["note"] == "m6", f"{label} read: {status} {body!r}")
    status, body = call(args, port, "PUT", f"/parts/{first}", b'{"quantity":9,"note":"steel"}')
    check(status == 200 and json.loads(body)["quantity"] == 9 and json.loads(body)["note"] == "steel",
          f"{label} update: {status} {body!r}")
    status, body = call(args, port, "PUT", "/parts/999999", b'{"quantity":1}')
    check(status == 404, f"{label} update of a missing part: {status} {body!r}")
    status, body = call(args, port, "POST", "/parts", b"not json")
    check(status == 400, f"{label} malformed body: {status} {body!r}")
    status, body = call(args, port, "DELETE", f"/parts/{second}")
    check(status == 204, f"{label} delete: {status} {body!r}")
    status, body = call(args, port, "GET", f"/parts/{second}")
    check(status == 404, f"{label} read after delete: {status} {body!r}")
    status, body = call(args, port, "DELETE", f"/parts/{second}")
    check(status == 404, f"{label} second delete: {status} {body!r}")


def concurrent_journey(args, port, label):
    """40 creates, 10 updates of one part and 10 lists at once, from 20 threads: the
    workers' leases share the host's pool on the host's threads."""
    status, body = call(args, port, "POST", "/parts", b'{"code":"shared","quantity":0}')
    check(status == 201, f"{label} shared part: {status} {body!r}")
    shared = json.loads(body)["id"] if status == 201 else 0
    before = len(json.loads(call(args, port, "GET", "/parts")[1]))
    jobs = [("POST", "/parts", f'{{"code":"c{i}","quantity":{i}}}'.encode(), 201) for i in range(40)]
    jobs += [("PUT", f"/parts/{shared}", f'{{"quantity":{i}}}'.encode(), 200) for i in range(10)]
    jobs += [("GET", "/parts", None, 200) for _ in range(10)]
    results = []
    lock = threading.Lock()

    def worker(batch):
        for method, path, body, expected in batch:
            try:
                status, _ = http_call(port, method, path, body)
            except OSError as error:
                status = repr(error)
            with lock:
                results.append((method, path, status, expected))

    threads = [threading.Thread(target=worker, args=(jobs[i::20],)) for i in range(20)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(120)
    wrong = [r for r in results if r[2] != r[3]]
    check(len(results) == len(jobs) and not wrong, f"{label} concurrent requests: {len(results)} done, wrong {wrong[:5]}")
    status, body = call(args, port, "GET", "/parts")
    after = len(json.loads(body)) if status == 200 else -1
    check(after == before + 40, f"{label} rows after the concurrent creates: {before} then {after}")


def http_call(port, method, path, body=None, timeout=60):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    connection.request(method, path, body=body)
    response = connection.getresponse()
    response.read()
    connection.close()
    return response.status, None


def probe_journey(args, port, label, host=None):
    status, body = call(args, port, "POST", "/setup")
    check(status == 200, f"{label} setup: {status} {body!r}")
    status, body = call(args, port, "POST", "/insert?a")
    check(status == 200 and body == b"1", f"{label} insert: {status} {body!r}")
    status, body = call(args, port, "POST", "/insert?a")
    check(status == 409 and body.startswith(b"sql.constraint_violation"), f"{label} duplicate: {status} {body!r}")
    check(b"'a'" not in body and b"=(a)" not in body, f"{label} the detail carries no row data: {body!r}")
    # One connection in the pool: a request that leaves its transaction open
    # must not keep it, and its insert must be rolled back.
    for attempt in range(3):
        status, body = call(args, port, "POST", "/leak")
        check(status == 200, f"{label} leak {attempt}: {status} {body!r}")
    status, body = call(args, port, "GET", "/count")
    check(status == 200 and body == b"1", f"{label} the leaked inserts were rolled back: {status} {body!r}")
    started = time.time()
    status, body = call(args, port, "GET", "/slow")
    elapsed = time.time() - started
    check(status == 504 and elapsed < PROBE_DEADLINE + 8,
          f"{label} a statement past the deadline: {status} {elapsed:.1f}s {body!r}"
          + ("" if status == 504 or host is None else f"\nhost log:\n{host.stderr()}"))
    status, body = call(args, port, "GET", "/count")
    check(status == 200 and body == b"1", f"{label} the connection is usable after the interrupt: {status} {body!r}")
    status, body = call(args, port, "GET", "/config?sql.main.url")
    check(status == 200 and body == b"none", f"{label} sql settings are not configuration: {status} {body!r}")
    status, body = call(args, port, "GET", "/config?sql.main.password")
    check(status == 200 and body == b"none", f"{label} passwords are not configuration: {status} {body!r}")
    status, body = call(args, port, "GET", "/many")
    pages = int(body.split(b" in ")[1]) if status == 200 and b" in " in body else 0
    check(status == 200 and body.startswith(b"1000 in ") and pages >= 2,
          f"{label} a cursor's pages fit the channel and lose no row: {status} {body!r}")
    status, body = call(args, port, "GET", "/big?1000")
    check(status == 200 and body == b"1000", f"{label} a 1,000-byte parameter: {status} {body!r}")
    status, body = call(args, port, "GET", "/big?1100000")
    check(status == 200 and body == b"sql.too_large", f"{label} a parameter beyond the channel's frame: {status} {body!r}")
    status, body = call(args, port, "GET", "/pool?main")
    check(status == 200, f"{label} pool lookup: {status} {body!r}")
    status, body = call(args, port, "GET", "/pool?other")
    check(status == 404 and b"NotFound" in body, f"{label} missing pool: {status} {body!r}")


# ---------------------------------------------------------------------------
# SQLite


def test_sqlite_crud(args):
    image = build(args, "examples/server/parts-sql.nra", "parts-sql.nvr")
    database = Path(args.work) / "parts.db"
    for path in Path(args.work).glob("parts.db*"):
        path.unlink()
    migrations = Path(args.source) / "examples/server/parts-migrations/sqlite"
    host = Host(args, image, "--sql-pool", f"main=sqlite:{database}", "--config", f"sql.main.migrations={migrations}",
                "--config", "sql.main.migrate=on_start")
    try:
        crud_journey(args, host.port, "sqlite")
        concurrent_journey(args, host.port, "sqlite")
    finally:
        host.stop()
    check("applied 0001_create_parts.sql" in host.stderr(), f"migrations applied at start: {host.stderr()}")
    # A second start finds nothing pending, even in manual mode.
    host = Host(args, image, "--sql-pool", f"main=sqlite:{database}", "--config", f"sql.main.migrations={migrations}")
    try:
        status, body = call(args, host.port, "GET", "/parts")
        check(status == 200 and len(json.loads(body)) == 42, f"data survives a restart: {status} {len(body)}")
    finally:
        host.stop()


def test_sqlite_probes(args):
    image = build(args, "tests/server/fixtures/sql-probe.nra", "sql-probe.nvr")
    database = Path(args.work) / "probe.db"
    for path in Path(args.work).glob("probe.db*"):
        path.unlink()
    env = {**os.environ, "SQLPROBE_sql.main.url": "leak", "SQLPROBE_sql.main.password": "leak"}
    host = Host(args, image, "--request-timeout", str(PROBE_DEADLINE), "--sql-pool", f"main=sqlite:{database}",
                "--config", "sql.main.max_connections=1", "--config", "sql.main.acquire_timeout=1",
                "--config-env-prefix", "SQLPROBE_", env=env)
    try:
        probe_journey(args, host.port, "sqlite", host)
        started = time.time()
        status, body = call(args, host.port, "GET", "/two")
        elapsed = time.time() - started
        check(status == 200 and body == b"sql.timed_out" and 0.8 < elapsed < 10,
              f"an acquire on an exhausted pool times out after S-10: {status} {body!r} {elapsed:.1f}s")
    finally:
        host.stop()
    host = Host(args, image, "--sql-pool", f"main=sqlite:{database}", "--config", "sql.main.max_connections=1",
                "--config", "sql.main.max_waiting=0")
    try:
        status, body = call(args, host.port, "GET", "/two")
        check(status == 200 and body == b"sql.pool_exhausted", f"no acquire may wait: {status} {body!r}")
    finally:
        host.stop()


def write_migrations(directory, files):
    shutil.rmtree(directory, ignore_errors=True)
    directory.mkdir(parents=True)
    for name, text in files.items():
        (directory / name).write_text(text)


def test_migrations(args):
    image = build(args, "tests/server/fixtures/sql-probe.nra", "sql-probe.nvr")
    directory = Path(args.work) / "migrations"
    database = Path(args.work) / "migrate.db"
    for path in Path(args.work).glob("migrate.db*"):
        path.unlink()
    pool = ["--sql-pool", f"main=sqlite:{database}", "--config", f"sql.main.migrations={directory}"]

    write_migrations(directory, {"0001_a.sql": "CREATE TABLE a (x INTEGER);", "0003_c.sql": "CREATE TABLE c (x INTEGER);"})
    result = run_host(args, image, "migrate", *pool)
    check(result.returncode == 1 and "E1044" in result.stderr and "0002 is missing before 0003_c.sql" in result.stderr,
          f"a gap is E1044: {result.returncode} {result.stderr}")
    write_migrations(directory, {"0001_a.sql": "CREATE TABLE a (x INTEGER);", "0001_b.sql": "CREATE TABLE b (x INTEGER);"})
    result = run_host(args, image, "migrate", *pool)
    check(result.returncode == 1 and "E1044" in result.stderr and "repeat the number 0001" in result.stderr,
          f"a repeated number is E1044: {result.stderr}")
    write_migrations(directory, {"0001_a.sql": "  \n"})
    result = run_host(args, image, "migrate", *pool)
    check(result.returncode == 1 and "E1044" in result.stderr and "0001_a.sql is empty" in result.stderr,
          f"an empty file is E1044: {result.stderr}")
    write_migrations(directory, {"0001_a.sql": "CREATE TABLE a (x INTEGER);", "1_bad.sql": "SELECT 1;"})
    result = run_host(args, image, "migrate", *pool)
    check(result.returncode == 1 and "E1044" in result.stderr and "1_bad.sql does not match" in result.stderr,
          f"a misnamed file is E1044: {result.stderr}")

    write_migrations(directory, {
        "0001_probe.sql": "CREATE TABLE probe (v TEXT NOT NULL UNIQUE);",
        "0002_seed.sql": "INSERT INTO probe (v) VALUES ('seed'); INSERT INTO probe (v) VALUES ('second');",
    })
    host = Host(args, image, "--listen", "127.0.0.1:0", *pool, expect_ready=False, listen=False)
    code = host.process.wait(60)
    host.stop()
    check(code == 1 and "pending migrations (0001_probe.sql, 0002_seed.sql)" in host.stderr(),
          f"manual mode refuses to start with pending migrations: {code} {host.stderr()}")
    result = run_host(args, image, "migrate", *pool)
    check(result.returncode == 0 and "applied 0002_seed.sql" in result.stderr,
          f"migrate applies and exits: {result.returncode} {result.stderr}")
    result = run_host(args, image, "migrate", *pool)
    check(result.returncode == 0 and "applied" not in result.stderr, f"a second migrate applies nothing: {result.stderr}")
    host = Host(args, image, *pool)
    try:
        status, body = call(args, host.port, "GET", "/count")
        check(status == 200 and body == b"2", f"the migrated data: {status} {body!r}")
    finally:
        host.stop()

    (directory / "0002_seed.sql").write_text("INSERT INTO probe (v) VALUES ('changed');")
    result = run_host(args, image, "--listen", "127.0.0.1:0", *pool)
    check(result.returncode == 1 and "0002_seed.sql was changed afterwards" in result.stderr,
          f"an edited applied migration refuses the start: {result.returncode} {result.stderr}")

    # A failing migration is rolled back and not recorded.
    (directory / "0002_seed.sql").write_text(
        "INSERT INTO probe (v) VALUES ('seed'); INSERT INTO probe (v) VALUES ('second');")
    (directory / "0003_broken.sql").write_text("INSERT INTO probe (v) VALUES ('third'); INSERT INTO nowhere VALUES (1);")
    result = run_host(args, image, "migrate", *pool)
    check(result.returncode == 1 and "migration 0003_broken.sql failed" in result.stderr,
          f"a failing migration: {result.returncode} {result.stderr}")
    (directory / "0003_broken.sql").unlink()
    host = Host(args, image, *pool)
    try:
        status, body = call(args, host.port, "GET", "/count")
        check(status == 200 and body == b"2", f"the failed migration left nothing: {status} {body!r}")
    finally:
        host.stop()


def test_record_replay(args):
    image = build(args, "tests/server/fixtures/sql-probe.nra", "sql-probe.nvr")
    database = Path(args.work) / "replay.db"
    for path in Path(args.work).glob("replay.db*"):
        path.unlink()
    traces = Path(args.work) / "sql-traces"
    shutil.rmtree(traces, ignore_errors=True)
    traces.mkdir()
    host = Host(args, image, "--record-dir", str(traces), "--no-startup-check", "--ready-workers", "0",
                "--sql-pool", f"main=sqlite:{database}")
    try:
        call(args, host.port, "POST", "/setup")
        status, body = call(args, host.port, "POST", "/insert?replayed")
        check(status == 200, f"recorded insert: {status} {body!r}")
    finally:
        host.stop()
    recorded = sorted(traces.glob("*.trace"), key=lambda path: path.stat().st_mtime)
    check(len(recorded) == 2, f"one trace per request: {recorded}")
    for path in Path(args.work).glob("replay.db*"):
        path.unlink()
    if recorded:
        result = subprocess.run([args.neura, "run", "--replay", str(recorded[-1]), str(image)],
                                capture_output=True, text=True, timeout=60)
        check(result.returncode == 0, f"replay without a host or a database: {result.returncode} {result.stderr}")
        check(not any(Path(args.work).glob("replay.db*")), "the replay opened no database")


def test_without_pools(args):
    image = build(args, "tests/server/fixtures/sql-probe.nra", "sql-probe.nvr")
    result = run_host(args, image, "--listen", "127.0.0.1:0", "--sql-pool", "other=sqlite::memory:")
    check(result.returncode == 1 and "startup check failed" in result.stderr,
          f"a missing pool fails the startup check: {result.returncode} {result.stderr}")
    result = subprocess.run([args.neura, "run", str(image)], capture_output=True, text=True, timeout=60)
    check(result.returncode == 1 and "CapabilityDenied" in result.stderr,
          f"neura run without pools: {result.returncode} {result.stderr}")
    result = run_host(args, image, "--listen", "127.0.0.1:0", "--sql-pool", "main=mysql://x",
                      "--config", "sql.main.colour=blue")
    check(result.returncode == 1 and "unknown SQL setting sql.main.colour" in result.stderr
          and "url is sqlite:<path> or a postgresql:// URI" in result.stderr,
          f"every configuration problem is listed: {result.stderr}")
    if os.name == "nt":
        result = run_host(args, image, "--listen", "127.0.0.1:0", "--sql-pool",
                          "main=postgresql://127.0.0.1:5432/probe")
        check(result.returncode == 1 and "the PostgreSQL provider is unavailable on Windows in Neura 0.3.0" in result.stderr,
              f"PostgreSQL is refused: {result.stderr}")


# ---------------------------------------------------------------------------
# PostgreSQL


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def make_certificates(openssl, directory, ca_name="neura-sql-test-ca"):
    """A CA and a server certificate for localhost and 127.0.0.1, signed by it."""
    directory.mkdir(parents=True, exist_ok=True)
    run = lambda *command: subprocess.run([openssl, *command], cwd=directory, capture_output=True, check=True)
    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "ca.key", "-out", "ca.crt", "-days", "2",
        "-subj", f"/CN={ca_name}")
    run("req", "-newkey", "rsa:2048", "-nodes", "-keyout", "server.key", "-out", "server.csr",
        "-subj", "/CN=localhost")
    (directory / "server.ext").write_text("subjectAltName=DNS:localhost,IP:127.0.0.1\n")
    run("x509", "-req", "-in", "server.csr", "-CA", "ca.crt", "-CAkey", "ca.key", "-CAcreateserial",
        "-out", "server.crt", "-days", "2", "-extfile", "server.ext")
    for name in ("ca.crt", "server.crt", "server.key"):
        os.chmod(directory / name, 0o644)


class Postgres:
    def __init__(self, version, image, certificates=None):
        self.port = free_port()
        self.name = f"neura-sql-test-{version.replace('.', '-')}-{os.getpid()}"
        subprocess.run(["docker", "rm", "-f", self.name], capture_output=True)
        command = ["docker", "run", "-d", "--rm", "--name", self.name, "--network", "host",
                   "-e", f"POSTGRES_PASSWORD={PG_PASSWORD}"]
        server = ["-c", "listen_addresses=127.0.0.1", "-c", f"port={self.port}"]
        if certificates:
            # The server's key must belong to the postgres user.
            command += ["-v", f"{certificates}:/certs:ro", "--entrypoint", "sh", image, "-c",
                        "cp /certs/server.crt /certs/server.key /tmp/ && chown postgres /tmp/server.key && "
                        "chmod 600 /tmp/server.key && exec docker-entrypoint.sh postgres " + " ".join(server) +
                        " -c ssl=on -c ssl_cert_file=/tmp/server.crt -c ssl_key_file=/tmp/server.key"]
        else:
            command += [image, *server]
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            raise RuntimeError(f"cannot start PostgreSQL {version}: {result.stderr}")
        deadline = time.time() + 60
        while time.time() < deadline:
            # The entry point restarts the server once after initdb; a query
            # over TCP succeeds only on the final server.
            if self.psql("SELECT 1").stdout.strip() == "1":
                return
            time.sleep(0.5)
        self.stop()
        raise RuntimeError(f"PostgreSQL {version} did not become ready")

    def url(self, database="postgres"):
        return f"postgresql://postgres@127.0.0.1:{self.port}/{database}?sslmode=disable"

    def psql(self, sql, database="postgres"):
        return subprocess.run(["docker", "exec", "-e", f"PGPASSWORD={PG_PASSWORD}", self.name, "psql", "-h",
                               "127.0.0.1", "-p", str(self.port), "-U", "postgres", "-d", database, "-At", "-c", sql],
                              capture_output=True, text=True, timeout=60)

    def stop(self):
        subprocess.run(["docker", "rm", "-f", self.name], capture_output=True, timeout=60)


def test_postgres(args):
    env = {**os.environ, "NEURA_SQL_TEST_PASSWORD": PG_PASSWORD}
    parts = build(args, "examples/server/parts-sql.nra", "parts-sql.nvr")
    probe = build(args, "tests/server/fixtures/sql-probe.nra", "sql-probe.nvr")
    migrations = Path(args.source) / "examples/server/parts-migrations/postgresql"
    tests = Path(args.source) / "tests/server/fixtures/sql-postgres-tests.nra"
    for version, image in PG_IMAGES.items():
        label = f"postgresql {version}"
        print("--", label, flush=True)
        server = Postgres(version, image)
        try:
            server.psql("CREATE DATABASE parts")
            server.psql("CREATE DATABASE probe")
            server.psql("CREATE DATABASE unit")
            result = subprocess.run([args.neura, "test", "--sql-pool", f"main={server.url('unit')}",
                                     "--sql-pool", "main.password_env=NEURA_SQL_TEST_PASSWORD", str(tests)],
                                    capture_output=True, text=True, env=env, timeout=300)
            check(result.returncode == 0, f"{label} neura test:\n{result.stdout}{result.stderr}")

            host = Host(args, parts, "--sql-pool", f"main={server.url('parts')}",
                        "--config", "sql.main.password_env=NEURA_SQL_TEST_PASSWORD",
                        "--config", f"sql.main.migrations={migrations}", "--config", "sql.main.migrate=on_start",
                        env=env)
            try:
                crud_journey(args, host.port, label)
                concurrent_journey(args, host.port, label)
            finally:
                host.stop()
            check(PG_PASSWORD not in host.stderr(), f"{label}: the password is not logged")
            applied = server.psql("SELECT version || ':' || name FROM neura_migrations ORDER BY version", "parts")
            check(applied.stdout.split() == ["1:create_parts", "2:add_note"], f"{label} neura_migrations: {applied.stdout!r}")

            host = Host(args, probe, "--request-timeout", str(PROBE_DEADLINE), "--sql-pool", f"main={server.url('probe')}",
                        "--config", "sql.main.password_env=NEURA_SQL_TEST_PASSWORD",
                        "--config", "sql.main.max_connections=1", "--config", "sql.main.acquire_timeout=1",
                        "--config", "probe.db=postgresql", env=env)
            try:
                probe_journey(args, host.port, label, host)
                sleeping = server.psql("SELECT count(*) FROM pg_stat_activity WHERE query LIKE 'SELECT pg_sleep%'")
                check(sleeping.stdout.strip() == "0", f"{label}: PQcancel ended the statement: {sleeping.stdout!r}")
            finally:
                host.stop()

            result = run_host(args, probe, "--listen", "127.0.0.1:0", "--sql-pool",
                              f"main=postgresql://postgres:{PG_PASSWORD}@127.0.0.1:{server.port}/probe?sslmode=disable")
            check(result.returncode == 1 and "holds a password" in result.stderr and PG_PASSWORD not in result.stderr,
                  f"{label}: a password in the URL is refused: {result.stderr}")
            result = run_host(args, probe, "--listen", "127.0.0.1:0", "--sql-pool",
                              "main=postgresql://postgres@db.example.com/probe?sslmode=disable")
            check(result.returncode == 1 and "allowed only for a loopback host" in result.stderr,
                  f"{label}: sslmode lowered for a remote host: {result.stderr}")
            result = run_host(args, probe, "--listen", "127.0.0.1:0", "--sql-pool",
                              f"main=postgresql://postgres@127.0.0.1:{server.port}/probe",
                              "--config", "sql.main.password_env=NEURA_SQL_TEST_PASSWORD", env=env)
            check(result.returncode == 1 and "cannot connect" in result.stderr,
                  f"{label}: verify-full by default refuses a server without TLS: {result.stderr}")
        finally:
            server.stop()
        if not args.openssl:
            continue
        # verify-full against a server with TLS: the CA the URL names, the
        # host name in the certificate, and a CA that did not sign it.
        certificates = Path(args.work) / "pg-certificates"
        shutil.rmtree(certificates, ignore_errors=True)
        make_certificates(args.openssl, certificates)
        other = Path(args.work) / "pg-other-ca"
        shutil.rmtree(other, ignore_errors=True)
        # A distinct CA name tests a missing trust path. Reusing the issuer's
        # name can instead make OpenSSL try the wrong key and report an RSA error.
        make_certificates(args.openssl, other, "neura-sql-unrelated-ca")
        server = Postgres(version, image, certificates)
        try:
            for host_name, ca, expected in (("localhost", certificates, True), ("127.0.0.1", certificates, True),
                                            ("localhost", other, False)):
                url = (f"main=postgresql://postgres@{host_name}:{server.port}/postgres"
                       f"?sslrootcert={ca / 'ca.crt'}")
                tls = Host(args, probe, "--sql-pool", url, "--config", "sql.main.password_env=NEURA_SQL_TEST_PASSWORD",
                           env=env, expect_ready=False)
                try:
                    if expected:
                        check(tls.ready, f"{label}: verify-full over TLS to {host_name}: {tls.stderr()}")
                        if tls.ready:
                            status, body = call(args, tls.port, "POST", "/setup")
                            check(status == 200, f"{label}: a statement over TLS: {status} {body!r}")
                    else:
                        check(not tls.ready and "certificate verify failed" in tls.stderr(),
                              f"{label}: a certificate from another CA is refused: {tls.stderr()}")
                finally:
                    tls.stop()
        finally:
            server.stop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--neura", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--curl")
    parser.add_argument("--openssl", help="makes certificates for the PostgreSQL TLS checks")
    parser.add_argument("--postgres", action="store_true", help="run the PostgreSQL tests instead")
    parser.add_argument("--postgres-build", action="store_true", help="the host has the PostgreSQL provider")
    parser.add_argument("--only")
    args = parser.parse_args()
    os.makedirs(args.work, exist_ok=True)
    if args.postgres:
        if not args.postgres_build:
            print("SKIP: the build has no PostgreSQL provider (NEURA_ENABLE_POSTGRES)")
            return SKIP
        if not shutil.which("docker") or subprocess.run(["docker", "info"], capture_output=True).returncode != 0:
            print("SKIP: Docker is not available")
            return SKIP
        tests = [test_postgres]
    else:
        tests = [test_sqlite_crud, test_sqlite_probes, test_migrations, test_record_replay, test_without_pools]
    for test in tests:
        if args.only and args.only != test.__name__:
            continue
        print("--", test.__name__, flush=True)
        try:
            test(args)
        except Exception as error:  # noqa: BLE001 - reported as a failure
            check(False, f"{test.__name__}: {error!r}")
    if FAILURES:
        print(f"{len(FAILURES)} failure(s)")
        return 1
    print("all SQL host checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
