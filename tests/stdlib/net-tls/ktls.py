#!/usr/bin/env python3
"""Qualify TLS record offload on Linux when the kernel and OpenSSL support it, else check its refusal."""

import errno
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile


# TLS 1.3 suites and their Linux UAPI cipher IDs and key widths.
CIPHERS = {"TLS_AES_128_GCM_SHA256": (51, 16), "TLS_AES_256_GCM_SHA384": (52, 32)}
UNAVAILABLE = {errno.ENOENT, errno.ENOPROTOOPT, errno.EOPNOTSUPP, errno.EPERM, errno.EACCES, errno.EINVAL}


def probe(cipher, key_bytes):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        with socket.create_connection(listener.getsockname(), timeout=2) as client:
            peer, _ = listener.accept()
            with peer:
                # Linux UAPI tls12_crypto_info_aes_gcm_{128,256}, also used for TLS 1.3.
                # Test that the kernel accepts both directions without exchanging data.
                key = struct.pack(f"=HH8s{key_bytes}s4s8s", 0x0304, cipher, b"", b"", b"", b"")
                client.setsockopt(socket.IPPROTO_TCP, 31, b"tls")
                client.setsockopt(282, 1, key)
                client.setsockopt(282, 2, key)


def refuse(tests, source, env, reason):
    # Without kernel TLS the documented behaviour is the refusal: kernel_offload
    # and TlsStream.enable_kernel_offload return Unsupported and the stream
    # keeps working in user space. OPENSSL_CONF is left unset here.
    print(f"kernel TLS: refusal path, because {reason}; checking that offload is refused with Unsupported",
          flush=True)
    subprocess.run([tests, "ktls_refusal", source], env=env, timeout=20, check=True)
    return 0


def main():
    tests, source, openssl = sys.argv[1:]
    env = dict(os.environ, PATH=str(Path(openssl).resolve().parent) + os.pathsep + os.environ.get("PATH", ""))
    disabled = subprocess.check_output([openssl, "list", "-disabled"], text=True, timeout=5)
    if "KTLS" in {line.strip().upper() for line in disabled.splitlines()}:
        return refuse(tests, source, env, "OpenSSL was built without kTLS")
    tested = False
    with tempfile.TemporaryDirectory(prefix="neura-ktls-") as directory:
        config = Path(directory) / "openssl.cnf"
        strict_env = dict(env, OPENSSL_CONF=str(config))
        unavailable = []
        for suite, (cipher, key_bytes) in CIPHERS.items():
            try:
                probe(cipher, key_bytes)
            except OSError as error:
                if error.errno not in UNAVAILABLE:
                    raise
                print(f"kernel TLS precondition unavailable for {suite}: {error}", flush=True)
                unavailable.append(f"{suite}: {error}")
                continue
            # The backend test uses TLS 1.3 on both endpoints. Give both SSL
            # contexts only the suite whose kernel support was just checked.
            config.write_text(
                "config_diagnostics = 1\n"
                "openssl_conf = init\n"
                "[init]\n"
                "ssl_conf = ssl_settings\n"
                "[ssl_settings]\n"
                "system_default = loopback\n"
                "[loopback]\n"
                f"Ciphersuites = {suite}\n"
            )
            print(f"kernel TLS: strict path with {suite}, because the kernel accepted its keys", flush=True)
            subprocess.run([tests, "ktls", source], env=strict_env, timeout=20, check=True)
            tested = True
    if tested:
        return 0
    return refuse(tests, source, env, "the kernel TLS probe failed for every suite (" + "; ".join(unavailable) + ")")


if __name__ == "__main__":
    raise SystemExit(main())
