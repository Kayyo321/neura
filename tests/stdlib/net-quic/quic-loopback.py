#!/usr/bin/env python3
"""QUIC handles shared between tasks over loopback (lane net-a milestone A1-0).

Builds loopback/p1-spike.nra, runs it live several times, records one run and
replays it with every capability denied, and checks the trace:

1. the coordinator's accept_stream and the two readers' stream_read were
   all pending at once (host-quic-provider shows their completion in one
   driver pass, which a live run cannot force: it depends on when the
   datagrams arrive);
2. the replay reproduces the output and touches no host service;
3. a closed connection's ID fails with Closed, also after a later connection
   reused its provider slot with a new generation;
4. a second read on a stream with a read pending fails at once (quic.busy);
5. the peer's application close code reaches a reader, and the trace records
   it in the read's success value ([bytes, ending 2, code 7]).
"""

import argparse
import pathlib
import re
import subprocess
import tempfile
import threading


def run(command, expect=0):
    result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    assert result.returncode == expect, (command, result.returncode, result.stdout, result.stderr)
    return result.stdout


def requests_and_deliveries(trace):
    lines = trace.read_text().splitlines()
    requests = [line for line in lines if line.startswith("request ")]
    delivered = {}
    for line in lines:
        if line.startswith("deliver "):
            parts = line.split()
            delivered[int(parts[2])] = int(parts[1])
    return requests, delivered


def check_parked_together(trace):
    """The coordinator's third accept_stream and the two readers' reads were
    all pending at once: each was issued before any of them was delivered.
    Returns their delivery ticks (equal when the datagrams arrived together;
    host-quic-provider checks one driver pass deterministically)."""
    requests, delivered = requests_and_deliveries(trace)
    accepts = [i for i, line in enumerate(requests) if "neura.net.quic.accept_stream" in line]
    assert len(accepts) == 3, accepts
    third = accepts[2]
    reads = [i for i in range(third + 1, len(requests)) if "neura.net.quic.stream_read" in requests[i]][:2]
    group = [third] + reads
    tasks = {re.search(r" task (\d+) ", requests[i]).group(1) for i in group}
    assert len(tasks) == 3, ("three different tasks", [requests[i] for i in group])
    issued = [int(re.search(r" tick (\d+)", requests[i]).group(1)) for i in group]
    ticks = [delivered[i] for i in group]
    assert max(issued) < min(ticks), ("all three pending before any delivery", issued, ticks)
    return ticks


def check_generations(trace):
    requests, _ = requests_and_deliveries(trace)
    connects = [i for i, line in enumerate(requests) if "neura.net.quic.connect" in line]
    assert len(connects) == 2, connects
    lines = trace.read_text().splitlines()
    ids = []
    for index in connects:
        at = lines.index(requests[index])
        ids.append(int(re.match(r"ok \[(\d+),", lines[at + 1]).group(1)))
    first, second = ids
    slot = (1 << 24) - 1
    assert first & slot == second & slot and first != second, ("same slot, new generation", ids)
    stale = [line for line in requests if "neura.net.quic.stream_write" in line and f"[{first}, 0, \"stale\"" in line]
    assert len(stale) == 1, "the stale handle was used after the slot was reused"


def check_close_payload(trace):
    lines = trace.read_text().splitlines()
    closes = [i for i, line in enumerate(lines) if line.startswith("request neura.net.quic.close") and ", 7, " in line]
    assert len(closes) == 1, closes
    reads = [i for i, line in enumerate(lines) if line.startswith("request neura.net.quic.stream_read") and i > closes[0]]
    answers = [lines[i + 1] for i in reads]
    assert any(re.fullmatch(r"ok \[0, 2, 7\]", answer) for answer in answers), answers


def no_tasks(neura, root, cert, key, mode, size):
    """Images without tasks: nothing services a connection between calls, so
    a client that writes and then closes, or that just ends, relies on close
    and on the run's end flushing what OpenSSL accepted."""
    images = {}
    here = pathlib.Path(__file__).parent / "loopback"
    for name in ("no-task-server", "no-task-client"):
        images[name] = root / f"{name}.nvr"
        if not images[name].exists():
            run([str(neura), "build", str(here / f"{name}.nra"), "-o", str(images[name])])
    server = subprocess.Popen([str(neura), "run", "--allow-net", "--allow-fs", str(images["no-task-server"]), "--",
                               str(cert), str(key), str(size)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True)
    try:
        line = server.stdout.readline()
        port = re.fullmatch(r"PORT (\d+)\n", line)
        assert port, line
        client = run([str(neura), "run", "--allow-net", "--allow-fs", str(images["no-task-client"]), "--",
                      port.group(1), str(cert), str(size), mode])
        out, err = server.communicate(timeout=60)
    finally:
        if server.poll() is None:
            server.kill()
    assert server.returncode == 0, (server.returncode, out, err)
    return client, out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neura", type=pathlib.Path, required=True)
    parser.add_argument("--openssl", type=pathlib.Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    args = parser.parse_args()
    assert re.match(r"OpenSSL 4\.1\.0\b", run([str(args.openssl), "version"])), "requires pinned OpenSSL 4.1.0"
    here = pathlib.Path(__file__).parent
    expected = (here / "loopback" / "p1-spike.expected").read_text()
    with tempfile.TemporaryDirectory(prefix="neura-quic-loopback-") as directory:
        root = pathlib.Path(directory)
        cert, key = root / "server.pem", root / "server.key"
        run([str(args.openssl), "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
             "-days", "2", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
             "-keyout", str(key), "-out", str(cert)])
        image = root / "p1-spike.nvr"
        run([str(args.neura), "build", str(here / "loopback" / "p1-spike.nra"), "-o", str(image)])
        program = ["--", str(cert), str(key)]
        for attempt in range(args.runs):
            trace = root / f"live-{attempt}.trace"
            live = run([str(args.neura), "run", "--allow-net", "--allow-fs", "--record", str(trace), str(image)]
                       + program)
            assert live == expected, (attempt, live)
            ticks = check_parked_together(trace)
            check_generations(trace)
            check_close_payload(trace)
            print(f"  run {attempt}: accept_stream and two reads pending together, delivered at ticks {ticks}",
                  flush=True)
        print(f"PASS live runs ({args.runs}): output, three tasks parked at once, generation-checked IDs", flush=True)
        replayed = run([str(args.neura), "run", "--deny-all", "--replay", str(root / "live-0.trace"), str(image)]
                       + program)
        assert replayed == expected, replayed
        # The replay takes the recorded quantum and limits, whatever it is given.
        replayed = run([str(args.neura), "run", "--deny-all", "--replay", str(root / "live-0.trace"),
                        "--task-quantum", "3", str(image)] + program)
        assert replayed == expected, replayed
        print("PASS replay with every capability denied reproduces the run", flush=True)
        size = 300000
        for mode, expected in (("close", "client closed"), ("exit", "client ends without closing")):
            client, server = no_tasks(args.neura, root, cert, key, mode, size)
            assert client == f"client wrote {size} bytes\n{expected}\n", client
            assert server == f"server read {size} bytes\n", server
        print("PASS without tasks, close and the run's end both deliver every byte written", flush=True)


if __name__ == "__main__":
    main()
