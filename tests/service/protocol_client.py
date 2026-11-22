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
import sys
from pathlib import Path

WINDOWS = sys.platform == "win32"

# Windows (docs/design/Windows030.md, W29): CPython has no AF_UNIX there, so
# a connection is made through Winsock with ctypes and then wrapped in a
# socket object. Handles travel in-band: the sender duplicates each into the
# receiving process and lists their values there in the extension field
# HANDLES (u64 each), as the service's own sender does (src/service/system.hpp).
HANDLES = 0x8001
if WINDOWS:
    import ctypes
    import msvcrt
    from ctypes import wintypes

    _ws2 = ctypes.WinDLL("ws2_32", use_last_error=True)
    _kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _ws2.socket.restype = ctypes.c_size_t
    _ws2.socket.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
    _ws2.connect.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int]
    _ws2.bind.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int]
    _ws2.closesocket.argtypes = [ctypes.c_size_t]
    _ws2.WSAIoctl.argtypes = [ctypes.c_size_t, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p,
                              wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p, ctypes.c_void_p]
    _kernel32.OpenProcess.restype = wintypes.HANDLE
    _kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    _kernel32.DuplicateHandle.argtypes = [wintypes.HANDLE, wintypes.HANDLE, wintypes.HANDLE,
                                          ctypes.POINTER(wintypes.HANDLE), wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    _kernel32.GetCurrentProcess.restype = wintypes.HANDLE
    _kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    SIO_AF_UNIX_GETPEERPID = 0x58000100
    PROCESS_DUP_HANDLE = 0x0040
    DUPLICATE_SAME_ACCESS = 0x2

    class _SockaddrUn(ctypes.Structure):
        _fields_ = [("sun_family", ctypes.c_ushort), ("sun_path", ctypes.c_char * 108)]

    def _address(path: Path) -> _SockaddrUn:
        return _SockaddrUn(1, str(path).encode("utf-8"))

    def unix_socket(path: Path) -> socket.socket:
        """A connected AF_UNIX stream socket."""
        socket.socket().close()  # Winsock is started
        handle = _ws2.socket(1, socket.SOCK_STREAM, 0)
        if handle == (1 << 64) - 1:
            raise OSError(ctypes.get_last_error(), "cannot create an AF_UNIX socket")
        address = _address(path)
        if _ws2.connect(handle, ctypes.byref(address), ctypes.sizeof(address)) != 0:
            error = ctypes.get_last_error()
            _ws2.closesocket(handle)
            raise ConnectionRefusedError(error, f"cannot connect to {path}")
        return socket.socket(fileno=handle)

    def bound_file(path: Path) -> None:
        """Binds an AF_UNIX socket to `path` and closes it, leaving the file."""
        socket.socket().close()
        handle = _ws2.socket(1, socket.SOCK_STREAM, 0)
        address = _address(path)
        _ws2.bind(handle, ctypes.byref(address), ctypes.sizeof(address))
        _ws2.closesocket(handle)

    def peer_pid(connection: socket.socket) -> int:
        pid = wintypes.DWORD(0)
        size = wintypes.DWORD(0)
        if _ws2.WSAIoctl(connection.fileno(), SIO_AF_UNIX_GETPEERPID, None, 0, ctypes.byref(pid), 4,
                         ctypes.byref(size), None, None) != 0:
            raise OSError(ctypes.get_last_error(), "SIO_AF_UNIX_GETPEERPID")
        return pid.value

    def give_handles(connection: socket.socket, fds: list[int]) -> bytes:
        """Duplicates the handles of C runtime descriptors `fds` into the peer
        and returns the value of the HANDLES field."""
        process = _kernel32.OpenProcess(PROCESS_DUP_HANDLE, False, peer_pid(connection))
        if not process:
            raise OSError(ctypes.get_last_error(), "OpenProcess")
        values = b""
        try:
            for fd in fds:
                theirs = wintypes.HANDLE()
                if not _kernel32.DuplicateHandle(_kernel32.GetCurrentProcess(), msvcrt.get_osfhandle(fd), process,
                                                 ctypes.byref(theirs), 0, False, DUPLICATE_SAME_ACCESS):
                    raise OSError(ctypes.get_last_error(), "DuplicateHandle")
                values += struct.pack("<Q", theirs.value or 0)
        finally:
            _kernel32.CloseHandle(process)
        return values

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
        if WINDOWS:
            self.socket = unix_socket(path)
            self.socket.settimeout(timeout)
        else:
            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.socket.settimeout(timeout)
            self.socket.connect(str(path))
        self.sequence = 0

    def close(self) -> None:
        self.socket.close()

    def send_raw(self, data: bytes, fds: list[int] | None = None) -> None:
        if fds and WINDOWS:
            raise NotImplementedError("a raw frame carries its handles in its own HANDLES field on Windows")
        if fds:
            socket.send_fds(self.socket, [data], fds)
        else:
            self.socket.sendall(data)

    def send(self, kind: str, fields: list[tuple[str, bytes]], fds: list[int] | None = None) -> int:
        """Sends a message; `fds` are descriptors (C runtime descriptors on
        Windows, whose handles are given to the peer)."""
        self.sequence += 1
        numbered = [(TAG[tag], value) for tag, value in fields]
        if fds and WINDOWS:
            numbered.append((HANDLES, give_handles(self.socket, fds)))
            fds = None
        self.send_raw(frame(KINDS[kind], numbered, self.sequence), fds)
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
            except (ConnectionResetError, ConnectionAbortedError):
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
        self.neurad = self.bin / ("neurad.exe" if WINDOWS else "neurad")
        self.build = subprocess.run([str(self.neurad), "--build"], capture_output=True, env=env,
                                    check=True).stdout.decode().strip()
        self.root = str(self.bin)
        self.generation = "dev"
        # Windows names an installation by its root in generic form (W29).
        key = self.bin.as_posix() if WINDOWS else self.root
        self.installation = hashlib.sha256(key.encode()).hexdigest()[:16]
        endpoint = hashlib.sha256(key.encode() + b"\0" + self.generation.encode()).hexdigest()[:16]
        if WINDOWS:
            self.directory = Path(env["LOCALAPPDATA"]) / "Neura" / "run" / endpoint
            self.state = Path(env["LOCALAPPDATA"]) / "Neura" / "state" / self.installation / self.generation
        else:
            self.directory = Path(env["XDG_RUNTIME_DIR"]) / "neura" / endpoint
            self.state = Path(env["XDG_STATE_HOME"]) / "neura" / self.installation / self.generation
        self.socket = self.directory / "s"
        self.process: subprocess.Popen | None = None

    def start(self, extra_env: dict | None = None, **kwargs) -> subprocess.Popen:
        import time
        env = dict(self.env, **(extra_env or {}))
        if WINDOWS:
            kwargs.setdefault("creationflags", subprocess.CREATE_NEW_PROCESS_GROUP)
        else:
            kwargs.setdefault("start_new_session", True)
        self.process = subprocess.Popen([str(self.neurad), "--foreground", "--root", self.root], env=env,
                                        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.PIPE, **kwargs)
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
    if WINDOWS:
        # No umask or nice value: the priority class stands in for nice (W29).
        mask = 0o22
        nice = ctypes.windll.kernel32.GetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess())
    else:
        mask = os.umask(0)
        os.umask(mask)
        nice = os.getpriority(os.PRIO_PROCESS, 0)
    fields += [("Policy", u8(63)), ("TaskDump", u8(0)), ("Umask", u32(mask)), ("Nice", u64(nice)),
               ("IgnoredSignals", u64(0)), ("BlockedSignals", u64(0)), ("Lifetime", u8(2 if detached else 1)),
               ("ClientClock", u64(0))]
    return fields


def memory_file(data: bytes) -> int:
    if WINDOWS:
        # A temporary file that is removed when its last handle closes.
        import tempfile
        fd, name = tempfile.mkstemp(prefix="test-image-")
        os.write(fd, data)
        os.close(fd)
        return os.open(name, os.O_RDONLY | os.O_BINARY | os.O_TEMPORARY)
    fd = os.memfd_create("test-image")
    view = memoryview(data)
    while view:
        view = view[os.write(fd, view):]
    return fd
