#!/usr/bin/env python3
"""Run a strict socket test only when its platform precondition is available."""

import argparse
import errno
from pathlib import Path
import socket
import subprocess
import sys

TESTS = {
    "raw": "test_raw_loopback",
    "mptcp": "test_mptcp_loopback",
    "udp-coalescing": "test_udp_segmentation_and_coalescing",
}
UNAVAILABLE = {errno.EACCES, errno.EPERM, errno.EINVAL, errno.ENOPROTOOPT, errno.EOPNOTSUPP,
               errno.EPROTONOSUPPORT, errno.EAFNOSUPPORT, 10013, 10022, 10042, 10043, 10045, 10047}


def probe(feature):
    if feature == "raw":
        with socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_ICMP):
            return
    if feature == "mptcp":
        if not sys.platform.startswith("linux"):
            raise OSError(errno.EOPNOTSUPP, "MPTCP provider requires Linux")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM, getattr(socket, "IPPROTO_MPTCP", 262)) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
        return
    if not (sys.platform.startswith("linux") or sys.platform == "win32"):
        raise OSError(errno.EOPNOTSUPP, "UDP receive coalescing requires Linux or Winsock")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
        option, value = (3, 65535) if sys.platform == "win32" else (104, 1)
        receiver.setsockopt(socket.IPPROTO_UDP, option, value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neura", type=Path, required=True)
    parser.add_argument("--backend-tests", type=Path)
    parser.add_argument("--feature", choices=TESTS, required=True)
    args = parser.parse_args()
    try:
        probe(args.feature)
    except OSError as error:
        if error.errno not in UNAVAILABLE and getattr(error, "winerror", None) not in UNAVAILABLE:
            raise
        print(f"{args.feature} platform precondition unavailable: {error}")
        return 77
    source = Path(__file__).with_name("optional-tests.nra")
    name = TESTS[args.feature]
    command = [str(args.neura.resolve()), "test", "--allow-net"]
    if args.feature == "raw":
        command.append("--allow-raw")
        if args.backend_tests:
            subprocess.run([str(args.backend_tests.resolve()), "raw_socket", str(source.parents[4])],
                           timeout=20, check=True)
    subprocess.run([*command, "--filter", f"{source}::OptionalSocketFeatures.{name}", str(source)],
                   timeout=90, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
