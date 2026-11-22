#!/usr/bin/env python3
"""Run a strict socket test when its platform precondition is available, else check the documented refusal.

The driver never skips. When the probe finds the precondition missing for a reason whose library behaviour is
documented, it runs the refusal tests for that reason instead; any other probe failure fails the test.
"""

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
# Probe failures with a documented refusal, by feature (Winsock codes for Windows), and the refusal tests that
# check it, each with the capability options it runs under.
#   raw: the OS denies the privilege, which the provider reports as PermissionDenied; without --allow-raw the
#        capability check refuses first with CapabilityDenied (docs/runtime/NVM.md, "Capabilities").
#   mptcp: no Multipath TCP, which neura.net.mptcp.* report as Unsupported (docs/runtime/NVM.md).
#   udp-coalescing: no UDP_GRO or UDP_RECV_MAX_COALESCED_SIZE, which option.set reports as Unsupported.
REFUSALS = {
    "raw": ({errno.EPERM, errno.EACCES, 10013},
            [("test_raw_open_refused_without_privilege", ["--allow-raw"]),
             ("test_raw_open_refused_without_capability", [])]),
    "mptcp": ({errno.EPROTONOSUPPORT, errno.ENOPROTOOPT, errno.EINVAL, errno.EAFNOSUPPORT, errno.EOPNOTSUPP,
               10022, 10043, 10045, 10047},
              [("test_mptcp_refused_as_unsupported", [])]),
    "udp-coalescing": ({errno.ENOPROTOOPT, errno.EOPNOTSUPP, 10042, 10045},
                       [("test_udp_coalescing_refused_as_unsupported", [])]),
}


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


def run_test(neura, source, root, name, options):
    relative = source.relative_to(root).as_posix()
    subprocess.run([str(neura.resolve()), "test", "--allow-net", *options, "--filter",
                    f"{relative}::OptionalSocketFeatures.{name}", str(source)], cwd=root, timeout=90, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neura", type=Path, required=True)
    parser.add_argument("--backend-tests", type=Path)
    parser.add_argument("--feature", choices=TESTS, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().with_name("optional-tests.nra")
    root = source.parents[4]
    try:
        probe(args.feature)
    except OSError as error:
        documented, refusals = REFUSALS[args.feature]
        if error.errno not in documented and getattr(error, "winerror", None) not in documented:
            print(f"{args.feature}: platform precondition unavailable for a reason with no documented refusal "
                  f"to check: {error}", flush=True)
            raise
        print(f"{args.feature}: refusal path, because the platform precondition is unavailable ({error}); "
              f"checking the documented error", flush=True)
        for name, options in refusals:
            run_test(args.neura, source, root, name, options)
        return 0
    print(f"{args.feature}: strict path, because the platform precondition is available", flush=True)
    options = []
    if args.feature == "raw":
        options.append("--allow-raw")
        if args.backend_tests:
            subprocess.run([str(args.backend_tests.resolve()), "raw_socket", str(root)],
                           timeout=20, check=True)
    run_test(args.neura, source, root, TESTS[args.feature], options)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
