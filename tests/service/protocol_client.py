"""A client of the runtime service's protocol 1.0 for tests
(docs/design/Service030.md, "Protocol"), written apart from the C++ codec so
that the supervisor is checked against a second reading of the note. It can
also send what the C++ client never would: malformed messages, a Commit with
the wrong number of descriptors, a Commit after a reservation expired.
"""

from __future__ import annotations

import hashlib
import os
import socket
import struct
import subprocess
from pathlib import Path

KINDS = {
    "Hello": 1, "Welcome": 2, "Prepare": 3, "Prepared": 4, "Image": 5, "Commit": 6, "Committed": 7,
    "Outcome": 8, "Rejected": 9, "Cancel": 10, "Signal": 11, "Query": 12, "Status": 13, "List": 14,
    "Listing": 15, "Stop": 16, "Shutdown": 17, "Retire": 18, "CacheClear": 19,
}
NAMES = {value: key for key, value in KINDS.items()}

TAG = {
    "ProtocolLow": 1, "ProtocolHigh": 2, "Build": 3, "Installation": 4, "Generation": 5, "Purpose": 6,
    "Feature": 7, "Protocol": 8, "Epoch": 9, "State": 10, "Descriptors": 11, "RequestId": 12, "Token": 13,
    "ValidMilliseconds": 14, "ImageNeeded": 15, "Path": 16, "ApplicationId": 17, "Terminal": 18,
    "ExitStatus": 19, "Time": 20, "Reason": 21, "Sentence": 22, "NotStarted": 23, "SignalNumber": 24,
    "GraceMilliseconds": 25, "Terminate": 26, "Record": 27, "Limit": 28, "ApplicationCount": 29,
    "CacheBytes": 30, "ReadyWorkers": 31, "TrapClass": 32, "ImageDigest": 40, "ImageSize": 41,
    "ImagePath": 42, "Argument": 43, "Environment": 44, "Mode": 45, "Policy": 46, "FsPrefix": 47,
    "RunLimit": 48, "RunFlag": 49, "TaskDump": 50, "TraceEvents": 51, "Umask": 52, "ResourceLimit": 53,
    "Nice": 54, "IgnoredSignals": 55, "BlockedSignals": 56, "Lifetime": 57, "ClientClock": 58, "Pid": 59,
}
TERMINALS = ["", "prepared", "starting", "running", "exited", "trapped", "load-failed", "stopped", "client-lost",
             "signaled", "worker-lost", "unknown", "expired"]


def u8(value: int) -> bytes:
    return struct.pack("<B", value)


def u16(value: int) -> bytes:
    return struct.pack("<H", value)


def u32(value: int) -> bytes:
    return struct.pack("<I", value)


def u64(value: int) -> bytes:
    return struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF)


def frame(kind: int, fields: list[tuple[int, bytes]], sequence: int = 0, flags: int = 0) -> bytes:
    body = u16(kind) + u16(flags) + u64(sequence)
    for tag, value in fields:
        body += u16(tag) + u32(len(value)) + value
    return u32(len(body)) + body


class Message:
    def __init__(self, kind: str, fields: list[tuple[int, bytes]], sequence: int):
        self.kind = kind
        self.fields = fields
        self.sequence = sequence

    def all(self, tag: str) -> list[bytes]:
        return [value for number, value in self.fields if number == TAG[tag]]

    def get(self, tag: str) -> bytes | None:
        values = self.all(tag)
        return values[0] if values else None

    def number(self, tag: str) -> int | None:
        value = self.get(tag)
        return None if value is None else int.from_bytes(value, "little", signed=len(value) == 8 and tag in (
            "ExitStatus", "Nice"))

    def text(self, tag: str) -> str | None:
        value = self.get(tag)
        return None if value is None else value.decode()

    def __repr__(self) -> str:
        return f"<{self.kind} {[(n, v[:40]) for n, v in self.fields]}>"


class Connection:
    def __init__(self, path: Path, timeout: float = 10):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        self.socket.connect(str(path))
        self.sequence = 0

    def close(self) -> None:
        self.socket.close()

    def send_raw(self, data: bytes, fds: list[int] | None = None) -> None:
        if fds:
            socket.send_fds(self.socket, [data], fds)
        else:
            self.socket.sendall(data)

    def send(self, kind: str, fields: list[tuple[str, bytes]], fds: list[int] | None = None) -> int:
        self.sequence += 1
        self.send_raw(frame(KINDS[kind], [(TAG[tag], value) for tag, value in fields], self.sequence), fds)
        return self.sequence

    def receive(self) -> Message | None:
        header = self._exactly(4)
        if header is None:
            return None
        length = struct.unpack("<I", header)[0]
        body = self._exactly(length)
        if body is None:
            return None
        kind, _, sequence = struct.unpack("<HHQ", body[:12])
        fields = []
        at = 12
        while at < len(body):
            tag, size = struct.unpack("<HI", body[at:at + 6])
            fields.append((tag, body[at + 6:at + 6 + size]))
            at += 6 + size
        return Message(NAMES.get(kind, str(kind)), fields, sequence)

    def _exactly(self, count: int) -> bytes | None:
        data = b""
        while len(data) < count:
            try:
                chunk = self.socket.recv(count - len(data))
            except ConnectionResetError:
                return None
            if not chunk:
                return None
            data += chunk
        return data


class Service:
    """The supervisor of a build directory's source build, for one test."""

    def __init__(self, bin_dir: Path, env: dict):
        self.bin = bin_dir.resolve()
        self.env = env
        self.neurad = self.bin / "neurad"
        self.build = subprocess.run([str(self.neurad), "--build"], capture_output=True, env=env,
                                    check=True).stdout.decode().strip()
        self.root = str(self.bin)
        self.generation = "dev"
        self.installation = hashlib.sha256(self.root.encode()).hexdigest()[:16]
        endpoint = hashlib.sha256(self.root.encode() + b"\0" + self.generation.encode()).hexdigest()[:16]
        self.directory = Path(env["XDG_RUNTIME_DIR"]) / "neura" / endpoint
        self.socket = self.directory / "s"
        self.state = Path(env["XDG_STATE_HOME"]) / "neura" / self.installation / self.generation
        self.process: subprocess.Popen | None = None

    def start(self, extra_env: dict | None = None, **kwargs) -> subprocess.Popen:
        import time
        env = dict(self.env, **(extra_env or {}))
        self.process = subprocess.Popen([str(self.neurad), "--foreground", "--root", self.root], env=env,
                                        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.PIPE, start_new_session=True, **kwargs)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if self.socket.exists():
                try:
                    self.hello().close()
                    return self.process
                except OSError:
                    pass
            if self.process.poll() is not None:
                raise RuntimeError(f"neurad exited {self.process.returncode}: {self.process.stderr.read().decode()}")
            time.sleep(0.02)
        raise RuntimeError("neurad did not start")

    def hello(self, purpose: int = 1, build: str | None = None, installation: str | None = None,
              generation: str | None = None, low: int = 1, high: int = 1) -> Connection:
        connection = Connection(self.socket)
        connection.send("Hello", [("ProtocolLow", u16(low)), ("ProtocolHigh", u16(high)),
                                  ("Build", (build or self.build).encode()),
                                  ("Installation", (installation or self.installation).encode()),
                                  ("Generation", (generation or self.generation).encode()), ("Purpose", u8(purpose))])
        connection.welcome = connection.receive()
        return connection


def envelope(image: bytes | None = None, size: int | None = None, digest: bytes | None = None,
             path: str = "image.nvr", arguments: list[bytes] | None = None, environment: dict | None = None,
             detached: bool = False) -> list[tuple[str, bytes]]:
    if digest is None:
        digest = hashlib.sha256(image or b"").digest()
    if size is None:
        size = len(image or b"")
    fields = [("RequestId", os.urandom(16)), ("ImageDigest", digest), ("ImageSize", u64(size)),
              ("ImagePath", path.encode())]
    for argument in arguments or []:
        fields.append(("Argument", argument))
    for name, value in (environment if environment is not None else dict(os.environ)).items():
        fields.append(("Environment", f"{name}={value}".encode()))
    mask = os.umask(0)
    os.umask(mask)
    nice = os.getpriority(os.PRIO_PROCESS, 0)
    fields += [("Policy", u8(63)), ("TaskDump", u8(0)), ("Umask", u32(mask)), ("Nice", u64(nice)),
               ("IgnoredSignals", u64(0)), ("BlockedSignals", u64(0)), ("Lifetime", u8(2 if detached else 1)),
               ("ClientClock", u64(0))]
    return fields


def memory_file(data: bytes) -> int:
    fd = os.memfd_create("test-image")
    view = memoryview(data)
    while view:
        view = view[os.write(fd, view):]
    return fd
