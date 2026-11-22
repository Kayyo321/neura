#!/usr/bin/env python3
"""Qualify TLS record offload on Linux when the kernel and OpenSSL support it."""

import errno
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys


def probe():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        with socket.create_connection(listener.getsockname(), timeout=2) as client:
            peer, _ = listener.accept()
            with peer:
                # Linux UAPI tls12_crypto_info_aes_gcm_128, also used for TLS 1.3.
                # This probe admits the cipher in the kernel without exchanging data.
                key = struct.pack("=HH8s16s4s8s", 0x0304, 51, b"", b"", b"", b"")
                client.setsockopt(socket.IPPROTO_TCP, 31, b"tls")
                client.setsockopt(282, 1, key)
                client.setsockopt(282, 2, key)


def main():
    tests, source, openssl = sys.argv[1:]
    disabled = subprocess.check_output([openssl, "list", "-disabled"], text=True, timeout=5)
    if "KTLS" in {line.strip().upper() for line in disabled.splitlines()}:
        print("OpenSSL was built without kTLS")
        return 77
    try:
        probe()
    except OSError as error:
        if error.errno not in {errno.ENOENT, errno.ENOPROTOOPT, errno.EOPNOTSUPP, errno.EPERM, errno.EACCES}:
            raise
        print(f"kernel TLS precondition unavailable: {error}")
        return 77
    env = dict(os.environ, PATH=str(Path(openssl).resolve().parent) + os.pathsep + os.environ.get("PATH", ""))
    subprocess.run([tests, "ktls", source], env=env, timeout=30, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
