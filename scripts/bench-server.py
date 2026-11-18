#!/usr/bin/env python3
"""Measures the server host budgets B-S1, B-S2 and B-S4 to B-S6 (docs/design/Apps030.md, "Budgets").

A fixed-rate loader with no dependency: each of the connections is a thread on its own HTTP/1.1
keep-alive connection that sends at its share of the rate and measures each latency from the time
the request was due, so a slow server is not hidden by a loader that waits for it. The host serves
tests/server/fixtures/test-server.nra; the workload is `GET /info` (a JSON operation that touches no
database) and, for B-S6, one `GET /trap` in 1,000.

    python3 scripts/bench-server.py --neura build/neura --host build/neura-server-host \\
        --budget all --report results.json

The loader is Python: before a budget it measures its own ceiling against a host-free echo server,
and a result near that ceiling measures the loader, not the host. Run it on an otherwise idle
machine; numbers from a loaded machine are indicative only.
"""

import argparse
import http.client
import json
import os
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

SOURCE = Path(__file__).resolve().parent.parent


def percentile(values, fraction):
    if not values:
        return float("nan")
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round(fraction * (len(ordered) - 1)))))
    return ordered[index]


class Load:
    """Sends `rate` requests per second over `connections` keep-alive connections for `seconds`."""

    def __init__(self, port, rate, connections, seconds, path="/info", trap_every=0, closed_loop=False):
        self.port = port
        self.rate = rate
        self.connections = connections
        self.seconds = seconds
        self.path = path
        self.trap_every = trap_every
        self.closed_loop = closed_loop
        self.lock = threading.Lock()
        self.latencies = []
        self.rejected = []
        self.statuses = {}
        self.errors = 0
        self.sent = 0

    def worker(self, index, start, stop):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=60)
        interval = self.connections / self.rate if self.rate else 0
        due = start + index * (interval / self.connections if interval else 0)
        count = 0
        while True:
            now = time.perf_counter()
            if now >= stop:
                break
            if not self.closed_loop:
                if due > now:
                    time.sleep(due - now)
                if due >= stop:
                    break
            sent_at = due if not self.closed_loop else time.perf_counter()
            count += 1
            path = self.path
            with self.lock:
                self.sent += 1
                if self.trap_every and self.sent % self.trap_every == 0:
                    path = "/trap"
            try:
                connection.request("GET", path)
                response = connection.getresponse()
                response.read()
                status = response.status
            except Exception:  # noqa: BLE001 - counted as an error
                status = 0
                connection.close()
                connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=60)
            elapsed = time.perf_counter() - sent_at
            with self.lock:
                self.statuses[status] = self.statuses.get(status, 0) + 1
                if status == 503:
                    self.rejected.append(elapsed)
                elif status == 0:
                    self.errors += 1
                elif path != "/trap":
                    self.latencies.append(elapsed)
            due += interval
        connection.close()

    def run(self):
        start = time.perf_counter() + 0.05
        stop = start + self.seconds
        threads = [threading.Thread(target=self.worker, args=(i, start, stop)) for i in range(self.connections)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        completed = sum(count for status, count in self.statuses.items() if status not in (0,))
        return {
            "requests": self.sent,
            "completed": completed,
            "per_second": completed / self.seconds,
            "statuses": {str(k): v for k, v in sorted(self.statuses.items())},
            "errors": self.errors,
            "median_ms": percentile(self.latencies, 0.5) * 1000,
            "p95_ms": percentile(self.latencies, 0.95) * 1000,
            "p99_ms": percentile(self.latencies, 0.99) * 1000,
            "rejected_p95_ms": percentile(self.rejected, 0.95) * 1000 if self.rejected else None,
        }


class Echo(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):  # noqa: N802 - the standard library's name
        body = b'{"ok":true}'
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def loader_ceiling(connections, seconds):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Echo)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        return Load(server.server_address[1], 0, connections, seconds, closed_loop=True).run()
    finally:
        server.shutdown()


class Host:
    def __init__(self, args, image, *options):
        self.log = open(Path(args.work) / "host.err", "w")
        command = [args.host, "--listen", "127.0.0.1:0", "--max-workers", str(args.workers), *options, str(image)]
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=self.log, text=True)
        self.port = None
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("the host did not start; see host.err")
            if "listening on http://" in line:
                self.port = int(line.strip().rsplit(":", 1)[1])
            if line.strip().endswith("ready"):
                break

    def tree(self):
        # Workers are children of whichever host thread spawned them.
        children = set()
        for task in Path(f"/proc/{self.process.pid}/task").glob("*"):
            try:
                children.update((task / "children").read_text().split())
            except OSError:
                pass
        return 1 + len(children)

    def resident_kib(self):
        try:
            for line in Path(f"/proc/{self.process.pid}/status").read_text().splitlines():
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
        except OSError:
            pass
        return 0

    def descriptors(self):
        try:
            return len(os.listdir(f"/proc/{self.process.pid}/fd"))
        except OSError:
            return -1

    def stop(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            self.process.wait(60)
        self.log.close()


def sample_tree(host, stop_event, peak):
    while not stop_event.is_set():
        peak[0] = max(peak[0], host.tree())
        time.sleep(0.01)


def budget_s1(args, image):
    host = Host(args, image)
    try:
        Load(host.port, args.rate, args.connections, args.warmup).run()
        result = Load(host.port, args.rate, args.connections, args.seconds).run()
    finally:
        host.stop()
    result["budget"] = "median <= 5 ms, p95 <= 15 ms, p99 <= 40 ms"
    result["met"] = result["median_ms"] <= 5 and result["p95_ms"] <= 15 and result["p99_ms"] <= 40
    return result


def budget_s2(args, image):
    host = Host(args, image)
    try:
        Load(host.port, 0, args.connections, args.warmup, closed_loop=True).run()
        result = Load(host.port, 0, args.connections, args.seconds, closed_loop=True).run()
    finally:
        host.stop()
    result["budget"] = ">= 1500 requests per second with no 503"
    result["met"] = result["per_second"] >= 1500 and "503" not in result["statuses"]
    return result


def budget_s4(args, image, rate):
    host = Host(args, image)
    peak = [0]
    stop_event = threading.Event()
    sampler = threading.Thread(target=sample_tree, args=(host, stop_event, peak))
    sampler.start()
    try:
        result = Load(host.port, rate, args.connections * 2, args.saturation_seconds).run()
    finally:
        stop_event.set()
        sampler.join()
        host.stop()
    limit = args.workers + 2 + 1 + 1  # S-1 + S-2 + 1, and the host itself.
    result["rate"] = rate
    result["peak_processes"] = peak[0]
    result["budget"] = f"503 within 5 ms at p95; processes never above {limit} (host included)"
    rejected = result["rejected_p95_ms"]
    result["met"] = (rejected is None or rejected <= 5) and peak[0] <= limit
    return result


def budget_s5(args, image):
    host = Host(args, image, "--drain-timeout", "30")
    load = Load(host.port, args.rate, args.connections, 4)
    thread = threading.Thread(target=load.run)
    thread.start()
    time.sleep(2)
    started = time.perf_counter()
    host.process.send_signal(signal.SIGTERM)
    code = host.process.wait(60)
    exit_seconds = time.perf_counter() - started
    thread.join()
    host.log.close()
    result = {"exit_seconds": exit_seconds, "exit_status": code, "statuses": load.statuses, "errors": load.errors}
    result["budget"] = "exit within S-12 + 1 s (31 s); every request in flight completes or is reported"
    result["met"] = exit_seconds <= 31 and code == 0
    return result


def budget_s6(args, image, rate):
    host = Host(args, image)
    samples = []
    stop_event = threading.Event()

    def sample():
        begun = time.perf_counter()
        while not stop_event.is_set():
            samples.append((time.perf_counter() - begun, host.resident_kib()))
            stop_event.wait(5)

    sampler = threading.Thread(target=sample)
    sampler.start()
    try:
        result = Load(host.port, rate, args.connections, args.sustained_seconds, trap_every=1000).run()
        time.sleep(2)
        result["descriptors_after"] = host.descriptors()
        result["processes_after"] = host.tree()
    finally:
        stop_event.set()
        sampler.join()
        host.stop()
    later = [(t, kib) for t, kib in samples if t >= 120] or samples
    if len(later) >= 2:
        times = [t / 60 for t, _ in later]
        sizes = [kib for _, kib in later]
        mean_t = statistics.fmean(times)
        mean_s = statistics.fmean(sizes)
        slope = sum((t - mean_t) * (s - mean_s) for t, s in zip(times, sizes)) / max(
            1e-9, sum((t - mean_t) ** 2 for t in times))
        result["rss_slope_percent_per_minute"] = 100 * slope / max(1, mean_s)
    result["rss_kib"] = samples
    result["rate"] = rate
    result["budget"] = "RSS slope < 1%/min after minute 2; no descriptor or process left"
    result["met"] = result.get("rss_slope_percent_per_minute", 0) < 1 and result["processes_after"] <= 1 + 2
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--neura", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--work", default=None)
    parser.add_argument("--budget", default="all", choices=["all", "ceiling", "s1", "s2", "s4", "s5", "s6"])
    parser.add_argument("--connections", type=int, default=32)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--rate", type=float, default=500, help="B-S1 and B-S5 fixed rate, requests per second")
    parser.add_argument("--warmup", type=float, default=10)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--saturation-seconds", type=float, default=30)
    parser.add_argument("--sustained-seconds", type=float, default=600)
    parser.add_argument("--host-option", action="append", default=[], help="an extra neura-server-host option")
    parser.add_argument("--s2-rate", type=float, help="the measured B-S2 rate, for B-S4 and B-S6 run on their own")
    parser.add_argument("--report")
    args = parser.parse_args()
    args.work = args.work or tempfile.mkdtemp(prefix="bench-server-")
    image = Path(args.work) / "test-server.nvr"
    subprocess.run([args.neura, "build", str(SOURCE / "tests/server/fixtures/test-server.nra"), "-o", str(image)],
                   check=True)
    if args.host_option:
        global Host  # noqa: PLW0603 - extra options apply to every host the budgets start
        base = Host

        class WithOptions(base):
            def __init__(self, a, i, *options):
                super().__init__(a, i, *args.host_option, *options)

        Host = WithOptions
    report = {"cpus": os.cpu_count(),
              "load_average": os.getloadavg(), "options": vars(args).copy()}
    report["loader_ceiling"] = loader_ceiling(args.connections, min(args.seconds, 10))
    print("loader ceiling:", json.dumps(report["loader_ceiling"]), flush=True)
    chosen = ["s1", "s2", "s4", "s5", "s6"] if args.budget == "all" else [args.budget]
    s2_rate = args.s2_rate
    for name in chosen:
        if name == "ceiling":
            continue
        if name == "s1":
            report["B-S1"] = budget_s1(args, image)
        elif name == "s2":
            report["B-S2"] = budget_s2(args, image)
            s2_rate = report["B-S2"]["per_second"]
        elif name == "s4":
            report["B-S4"] = budget_s4(args, image, 2 * (s2_rate or 1500))
        elif name == "s5":
            report["B-S5"] = budget_s5(args, image)
        elif name == "s6":
            report["B-S6"] = budget_s6(args, image, (s2_rate or 1500) / 2)
        printable = {k: v for k, v in report[f"B-{name.upper()}"].items() if k != "rss_kib"}
        print(f"B-{name.upper()}:", json.dumps(printable), flush=True)
    report["load_average_after"] = os.getloadavg()
    if args.report:
        Path(args.report).write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
