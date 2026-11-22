#!/usr/bin/env python3
"""DTLS 1.3 public transports and OpenSSL interoperability on private loopback.

Every socket is bound to 127.0.0.1. Port zero and published bound addresses avoid
free-port races. Stdin barriers select application phases, not sleeps. The relay
never decrypts, edits protocol fields, or sends packets outside the two peers.

Upstream OpenSSL 4.1.0 issue coverage (ordinary regressions, not reproducers):
  #32878: The reciprocal update arrives before the client's pending update ACK.
          This does not test retransmission of another lost post-handshake record.
  #33277: deadline expiry exercises listener retries; it does not force a
          permanent receive-system-call failure in the provider's memory BIO.
  #33278: ordinary listener output is covered, but no retryable write failure is
          injected into its memory BIO (outgoing UDP is handled by the adapter).
  #33242: handshake flight loss/reordering is covered. No early ACK is crafted,
          so these tests do not establish the exact first-flight ACK ordering.
See https://openssl-library.org/post/2026-10-23-openssl-41-final-release/.
"""

import argparse
import collections
import contextlib
import pathlib
import queue
import re
import selectors
import socket
import subprocess
import tempfile
import threading
import time


WAIT = 15
CASES = ("round-trip", "mutual", "wrong-hostname", "wrong-ca", "missing-client-certificate",
         "handshake-drop", "handshake-reorder", "duplicate", "truncate", "loss-reorder",
         "idle-read", "receive-error",
         "client-timeout", "server-timeout", "denied-net", "openssl-server", "openssl-client",
         "key-update", "key-update-drop", "key-update-reorder")


def address(text):
    host, port = text.rsplit(":", 1)
    assert host == "127.0.0.1", text
    return host, int(port)


def wire_text(text):
    return text.ljust(256) + "\n"


def run(command, **options):
    result = subprocess.run(command, text=True, capture_output=True, timeout=60, **options)
    assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
    return result.stdout


class Peer:
    """Drain both pipes while allowing bounded, ordered protocol-marker reads."""

    def __init__(self, command):
        self.command = command
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, bufsize=1)
        self.output = {"stdout": [], "stderr": []}
        self.queues = {name: queue.Queue() for name in self.output}
        self.threads = []
        self.commands = []
        for name in self.output:
            thread = threading.Thread(target=self._read, args=(name,), daemon=True)
            thread.start()
            self.threads.append(thread)

    def _read(self, name):
        for line in getattr(self.process, name):
            text = line.rstrip("\r\n")
            self.output[name].append(text)
            self.queues[name].put(text)
        self.queues[name].put(None)

    def expect(self, expected, stream="stdout", exact=True, timeout=WAIT):
        limit = time.monotonic() + timeout
        while True:
            remaining = limit - time.monotonic()
            assert remaining > 0, ("marker timeout", expected, self.command, self.diagnostic())
            try:
                line = self.queues[stream].get(timeout=remaining)
            except queue.Empty:
                raise AssertionError(("marker timeout", expected, self.command, self.diagnostic())) from None
            assert line is not None, ("peer closed before marker", expected, self.command, self.diagnostic())
            matched = line == expected if exact else expected in line
            if matched:
                return line

    def diagnostic(self):
        diagnostic = {name: [line for line in lines if not re.match(r"^    [0-9a-f]{2}( |$)", line)][-25:]
                      for name, lines in self.output.items()}
        if hasattr(self, "related"):
            diagnostic["other-peer"] = self.related.diagnostic()
        diagnostic["commands"] = self.commands[-8:]
        if hasattr(self, "relay"):
            diagnostic["relay"] = self.relay.snapshot()
        return diagnostic

    def count(self, fragment, stream="stdout"):
        return sum(fragment in line for line in self.output[stream])

    def expect_count(self, fragment, count, stream="stdout", timeout=WAIT):
        limit = time.monotonic() + timeout
        while self.count(fragment, stream) < count:
            remaining = limit - time.monotonic()
            assert remaining > 0, ("marker count timeout", fragment, count, self.diagnostic())
            try:
                line = self.queues[stream].get(timeout=remaining)
            except queue.Empty:
                raise AssertionError(("marker count timeout", fragment, count, self.diagnostic())) from None
            assert line is not None, ("peer closed before marker count", fragment, count, self.diagnostic())

    def send(self, text="go\n"):
        assert self.process.poll() is None, (self.command, self.output)
        self.process.stdin.write(text)
        self.process.stdin.flush()
        self.commands.append((text[:16].rstrip(), len(text)))

    def finish(self):
        code = self.process.wait(timeout=WAIT)
        for thread in self.threads:
            thread.join(timeout=2)
        assert code == 0, (self.command, code, self.output)

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        for thread in self.threads:
            thread.join(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()


class Relay:
    """Two bound endpoints, with actions selected before releasing a barrier."""

    def __init__(self, handshake="plain"):
        self.sockets = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
        for sock in self.sockets:
            sock.bind(("127.0.0.1", 0))
        self.destinations = [None, None]
        self.lock = threading.Lock()
        self.mode = handshake
        self.events = collections.Counter()
        self.packets = collections.deque(maxlen=40)
        self.held = None
        self.failure = None
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def endpoint(self, side):
        host, port = self.sockets[side].getsockname()
        return f"{host}:{port}"

    def connect(self, side, target):
        with self.lock:
            self.destinations[side] = address(target)

    def arm(self, mode):
        with self.lock:
            assert self.held is None, (self.mode, self.events)
            self.mode = mode

    def _forward(self, side, data):
        target = self.destinations[1 - side]
        assert target is not None, ("missing relay destination", side)
        self.sockets[1 - side].sendto(data, target)

    def _packet(self, side, data):
        # Side zero is the client's ingress, side one the server's ingress.
        self.events[f"packets-{side}"] += 1
        self.packets.append((self.mode, side, len(data)))
        if self.mode == "handshake-drop" and side == 0 and not self.events["handshake-drop"]:
            self.events["handshake-drop"] += 1
            return
        # Let the plaintext cookie exchange through; reverse two encrypted
        # records in the fragmented server certificate flight itself.
        encrypted = bool(data) and data[0] & 0xE0 == 0x20
        if (self.mode == "handshake-reorder" and side == 1 and encrypted
                and not self.events["handshake-reorder"]):
            if self.held is None:
                self.held = (side, data)
                return
            held_side, held = self.held
            self._forward(side, data)
            self._forward(held_side, held)
            self.held = None
            self.events["handshake-reorder"] += 1
            self.events["distinct-handshake-reorder"] += held != data
            return
        # The fixture's application payload is 257 bytes. Client ACK and
        # KeyUpdate records are short; READY ensures its certificate flight
        # has already reached the server before application actions are armed.
        application = side == 0 and len(data) > 200
        if application and self.mode in {"duplicate", "truncate"} and not self.events[self.mode]:
            self.events[self.mode] += 1
            changed = data if self.mode == "duplicate" else data[:len(data) // 2]
            self._forward(side, changed)
        if application and self.mode == "loss-reorder":
            self.events["application-candidate"] += 1
            count = self.events["application-candidate"]
            if count == 1:
                self.events["application-drop"] += 1
                return
            if count == 2:
                self.held = (side, data)
                return
            if count == 3:
                self._forward(side, data)
                self._forward(*self.held)
                self.held = None
                self.events["application-reorder"] += 1
                return
        # With the pinned peers' five-byte unified header and 16-byte AEAD tag,
        # a standalone KeyUpdate is 35 bytes; an ACK is 40 bytes or longer.
        # The CLI transcript also has to show the matching update in each direction.
        if self.mode == "key-drop" and side == 0 and len(data) == 35 and not self.events["key-drop"]:
            self.events["key-drop"] += 1
            return
        if self.mode == "key-reorder" and side == 1:
            if len(data) == 40 and not self.events["key-reorder"] and self.held is None:
                self.held = (side, data)
                return
            elif len(data) == 35 and self.held is not None:
                # Reverse two distinct protocol packets: the reciprocal
                # KeyUpdate is delivered before the preceding ACK of the
                # client's update. No old update is replayed after app data.
                held_side, held = self.held
                assert held_side == side and len(held) == 40 and held != data
                self._forward(side, data)
                self._forward(held_side, held)
                self.held = None
                self.events["key-reorder"] += 1
                self.events["distinct-key-reorder"] += 1
                return
        self._forward(side, data)

    def _run(self):
        try:
            with selectors.DefaultSelector() as selected:
                for side, sock in enumerate(self.sockets):
                    selected.register(sock, selectors.EVENT_READ, side)
                while not self.stopped.is_set():
                    for key, _ in selected.select(0.05):
                        data, sender = key.fileobj.recvfrom(65535)
                        assert sender[0] == "127.0.0.1", sender
                        with self.lock:
                            side = key.data
                            if self.destinations[side] is None:
                                self.destinations[side] = sender
                            assert sender == self.destinations[side], (sender, self.destinations)
                            self._packet(side, data)
        except BaseException as failure:
            self.failure = failure

    def check(self, *events):
        assert self.failure is None, self.failure
        for event in events:
            assert self.events[event] > 0, ("relay action not exercised", event, self.events)

    def snapshot(self):
        with self.lock:
            return {"events": dict(self.events), "packets": list(self.packets), "held": self.held is not None}

    def close(self):
        self.stopped.set()
        self.thread.join(timeout=2)
        assert not self.thread.is_alive(), "relay failed to stop"
        for sock in self.sockets:
            sock.close()


def neura_peer(stack, runner, relay, server, certificate, key, ca, name="localhost", scenario="round-trip",
               options=()):
    side = 1 if server else 0
    command = runner + list(options) + ["--", "server" if server else "client", relay.endpoint(side),
                                        str(certificate), str(key), str(ca), name, scenario]
    peer = Peer(command)
    stack.callback(peer.close)
    line = peer.expect("ADDRESS ", exact=False)
    relay.connect(side, line.removeprefix("ADDRESS "))
    return peer


def complete_pair(client, server, scenario):
    client.send()
    server.send()
    if scenario != "loss-reorder":
        client.expect("NEXT")
        server.expect("NEXT")
        client.send()
        server.send()
    client.expect("DONE")
    server.expect("DONE")
    client.send()
    server.send()
    client.expect("RESULT ok")
    server.expect("RESULT ok")
    client.finish()
    server.finish()


def pair(runner, root, certificates, case, record=False):
    cert, key, client_cert, client_key, wrong_ca = certificates
    with contextlib.ExitStack() as stack:
        relay = Relay(case if case.startswith("handshake-") else "plain")
        stack.callback(relay.close)
        mutual = case in {"mutual", "missing-client-certificate"}
        ca = client_cert if mutual else ""
        server = neura_peer(stack, runner, relay, True, cert, key, ca,
                            scenario=case if case in {"loss-reorder", "mutual"} else "round-trip")
        trace = root / "dtls.trace"
        options = ["--record", str(trace)] if record else []
        client = neura_peer(stack, runner, relay, False,
                            client_cert if case == "mutual" else "", client_key if case == "mutual" else "",
                            wrong_ca if case == "wrong-ca" else cert,
                            "wrong.test" if case == "wrong-hostname" else "localhost",
                            case if case in {"loss-reorder", "idle-read", "receive-error", "mutual"}
                            else "round-trip", options)
        if case in {"wrong-hostname", "wrong-ca"}:
            client.expect("ERROR PermissionDenied")
            client.finish()
        elif case == "missing-client-certificate":
            server.expect("ERROR PermissionDenied")
            server.finish()
        else:
            client.expect("READY")
            server.expect("READY")
            if case in {"idle-read", "receive-error"}:
                client.expect("READ TimedOut" if case == "idle-read" else "READ ConnectionReset")
            if case == "mutual":
                client.expect("CHAIN verified")
                server.expect("CHAIN verified")
            elif case == "round-trip":
                server.expect("CHAIN PermissionDenied")
            relay.arm(case if case in {"duplicate", "truncate", "loss-reorder"} else "plain")
            complete_pair(client, server, case)
            expected = ["RECEIVED second", "RECEIVED first"] if case == "loss-reorder" else [
                "RECEIVED ping-one", "RECEIVED ping-two"]
            assert [line for line in server.output["stdout"] if line.startswith("RECEIVED ")] == expected
        events = {"handshake-drop": ["handshake-drop"],
                  "handshake-reorder": ["handshake-reorder", "distinct-handshake-reorder"],
                  "duplicate": ["duplicate"], "truncate": ["truncate"],
                  "loss-reorder": ["application-drop", "application-reorder"]}
        relay.check(*events.get(case, []))
        recorded = "\n".join(client.output["stdout"]) + "\n"
        replay_command = runner + ["--replay", str(trace)] + client.command[len(runner) + len(options):]
    if record:
        # Both live peers and the relay are gone. Replay must use the recorded
        # UDP, TLS engine, clock and console events without opening a socket.
        replayed = run(replay_command, input="go\ngo\ngo\n")
        assert replayed == recorded, (recorded, replayed)
        print("PASS public-round-trip-record-replay", flush=True)
    print(f"PASS public-{case}", flush=True)


def timeout(runner, certificates, server):
    cert, key, _, _, _ = certificates
    with contextlib.ExitStack() as stack:
        relay = Relay()
        stack.callback(relay.close)
        silent = stack.enter_context(socket.socket(socket.AF_INET, socket.SOCK_DGRAM))
        silent.bind(("127.0.0.1", 0))
        relay.connect(0 if server else 1, f"127.0.0.1:{silent.getsockname()[1]}")
        # Its other bound socket is deliberately silent, not an unbound port
        # whose ICMP rejection could replace the intended handshake timeout.
        peer = neura_peer(stack, runner, relay, server, cert if server else "", key if server else "",
                          "" if server else cert, scenario="timeout")
        started = time.monotonic()
        peer.expect("ERROR TimedOut", timeout=4)
        peer.finish()
        assert time.monotonic() - started < 4
        relay.check()
    print(f"PASS public-{'server' if server else 'client'}-timeout", flush=True)


def openssl_client(runner, openssl, certificates, key_update=False, drop=False, reorder=False):
    cert, key, client_cert, client_key, _ = certificates
    with contextlib.ExitStack() as stack:
        relay = Relay()
        stack.callback(relay.close)
        server = neura_peer(stack, runner, relay, True, cert, key, client_cert)
        client = Peer([str(openssl), "s_client", "-dtls1_3", "-connect", relay.endpoint(0),
                       "-servername", "localhost", "-verify_hostname", "localhost", "-verify_return_error",
                       "-CAfile", str(cert), "-cert", str(client_cert), "-key", str(client_key), "-msg"])
        stack.callback(client.close)
        client.related = server
        client.relay = relay
        server.expect("READY")
        server.send()
        client.send(wire_text("ping-one"))
        client.expect(wire_text("pong-one").rstrip("\n"))
        server.expect("NEXT")
        # Second exchange proves the connection still carries both directions
        # after the requested reciprocal update, not merely a buffered reply.
        server.send()
        if key_update:
            relay.arm("key-drop" if drop else "key-reorder" if reorder else "plain")
            acknowledgments = client.count(">>> DTLS 1.3, ACK")
            updates = client.count("<<< DTLS 1.3, Handshake [length 000d], KeyUpdate")
            client.send("K\n")
            client.expect("KEYUPDATE", stream="stderr")
            client.expect(">>> DTLS 1.3, Handshake [length 000d], KeyUpdate")
            client.expect("<<< DTLS 1.3, ACK", exact=False)
            if reorder:
                # The reciprocal update preceded the ACK in the reordered
                # output, so its callback may already have been consumed.
                client.expect_count("<<< DTLS 1.3, Handshake [length 000d], KeyUpdate", updates + 1)
                client.expect_count(">>> DTLS 1.3, ACK", acknowledgments + 1)
        client.send(wire_text("ping-two"))
        client.expect(wire_text("pong-two").rstrip("\n"))
        server.expect("DONE")
        server.send()
        server.expect("RESULT ok")
        server.finish()
        assert [line for line in server.output["stdout"] if line.startswith("RECEIVED ")] == [
            "RECEIVED ping-one", "RECEIVED ping-two"], server.diagnostic()
        assert client.count(wire_text("pong-two").rstrip("\n")) == 1, client.diagnostic()
        if key_update:
            assert any(line.startswith("<<< ") and line.endswith(", KeyUpdate")
                       for line in client.output["stdout"]), client.diagnostic()
            assert not any("NewSessionTicket" in line for line in client.output["stdout"]), client.diagnostic()
        relay.check(*(["key-drop"] if drop else ["key-reorder", "distinct-key-reorder"] if reorder else []))
    label = ("key-update-drop" if drop else "key-update-reorder" if reorder
             else "key-update" if key_update else "mutual-round-trip")
    print(f"PASS openssl-client-{label}", flush=True)


def openssl_server(runner, openssl, certificates):
    cert, key, client_cert, client_key, _ = certificates
    with contextlib.ExitStack() as stack:
        relay = Relay()
        stack.callback(relay.close)
        server = Peer([str(openssl), "s_server", "-dtls1_3", "-accept", "127.0.0.1:0", "-naccept", "1",
                       "-cert", str(cert), "-key", str(key), "-CAfile", str(client_cert), "-Verify", "1",
                       "-verify_return_error"])
        stack.callback(server.close)
        accepted = server.expect("ACCEPT 127.0.0.1:", exact=False)
        relay.connect(1, accepted.removeprefix("ACCEPT "))
        client = neura_peer(stack, runner, relay, False, client_cert, client_key, cert)
        client.expect("READY")
        client.send()
        server.expect(wire_text("ping-one").rstrip("\n"))
        server.send(wire_text("pong-one"))
        client.expect("NEXT")
        client.send()
        server.expect(wire_text("ping-two").rstrip("\n"))
        server.send(wire_text("pong-two"))
        client.expect("DONE")
        client.send()
        client.expect("RESULT ok")
        client.finish()
        relay.check()
    print("PASS openssl-server-mutual-round-trip", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neura", type=pathlib.Path, required=True)
    parser.add_argument("--openssl", type=pathlib.Path, required=True)
    parser.add_argument("--case", action="append", choices=CASES,
                        help="run only the selected case; repeat to select several")
    args = parser.parse_args()
    assert re.match(r"OpenSSL 4\.1\.0\b", run([str(args.openssl), "version"])), "requires pinned OpenSSL 4.1.0"
    with tempfile.TemporaryDirectory(prefix="neura-dtls-loopback-") as directory:
        root = pathlib.Path(directory)
        cert, key = root / "server.pem", root / "server.key"
        client_cert, client_key = root / "client.pem", root / "client.key"
        wrong_ca, wrong_key = root / "wrong.pem", root / "wrong.key"
        # A regular multi-SAN certificate makes the server flight span several
        # datagrams, so the reorder case can assert it reversed distinct bytes.
        san = "DNS:localhost," + ",".join(f"DNS:loopback-{index:02}.example.test" for index in range(64))
        for certificate, private, name, extensions in [
                (cert, key, "localhost", ["-addext", "subjectAltName=" + san]),
                (client_cert, client_key, "dtls-client", ["-addext", "extendedKeyUsage=clientAuth"]),
                (wrong_ca, wrong_key, "unrelated-root", [])]:
            run([str(args.openssl), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                 "-subj", f"/CN={name}", "-keyout", str(private), "-out", str(certificate)] + extensions)
        image = root / "peer.nvr"
        source = pathlib.Path(__file__).with_name("loopback") / "dtls-peer.nra"
        run([str(args.neura), "build", str(source), "-o", str(image)])
        runner = [str(args.neura), "run", str(image)]
        certificates = cert, key, client_cert, client_key, wrong_ca
        for case in args.case or CASES:
            if case in {"client-timeout", "server-timeout"}:
                timeout(runner, certificates, case == "server-timeout")
            elif case == "denied-net":
                denied = run(runner + ["--deny-net", "--", "client", "127.0.0.1:9", "", "", str(cert),
                                       "localhost", "timeout"])
                assert denied == "ERROR CapabilityDenied\n", denied
                print("PASS public-denied-net", flush=True)
            elif case == "openssl-server":
                openssl_server(runner, args.openssl, certificates)
            elif case in {"openssl-client", "key-update", "key-update-drop", "key-update-reorder"}:
                openssl_client(runner, args.openssl, certificates, key_update=case != "openssl-client",
                               drop=case == "key-update-drop", reorder=case == "key-update-reorder")
            else:
                pair(runner, root, certificates, case, record=case == "round-trip")


if __name__ == "__main__":
    main()
