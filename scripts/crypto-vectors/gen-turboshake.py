#!/usr/bin/env python3
"""Known answers for TurboSHAKE128 and TurboSHAKE256: the test vectors of RFC 9861, section 5,
https://www.rfc-editor.org/rfc/rfc9861.txt, copied from the fetched text.

Cases whose message is ptn(17**k) with k > 3 are left out (the NVM interprets
the permutation). A message is written as hex, or as "ptn:<n>" for the RFC's
pattern of n bytes 00, 01, ..., FA repeated. For the "last 32 bytes" cases the
row gives the number of bytes to skip.

    python3 scripts/crypto-vectors/gen-turboshake.py [--source-dir DIR]
"""

import argparse
import pathlib
import re
import sys
import tempfile
import urllib.request

from nra_vectors import write_module

URL = "https://www.rfc-editor.org/rfc/rfc9861.txt"
LABEL = re.compile(r"TurboSHAKE(128|256)\(M=(.*?), D=`([0-9A-F]{2})`, (\d+)\)(, last 32 bytes)?:")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=pathlib.Path)
    args = parser.parse_args()
    directory = args.source_dir or pathlib.Path(tempfile.mkdtemp())
    path = directory / "rfc9861.txt"
    if not path.exists():
        with urllib.request.urlopen(URL, timeout=60) as response:
            path.write_bytes(response.read())
    text = re.sub(r"\n\n[^\n]*\[Page \d+\]\n\f?\nRFC 9861[^\n]*\n\n", "\n", path.read_text())
    rows = []
    for match in LABEL.finditer(text):
        bits, message, domain, length, last = match.groups()
        value = re.search(r"`([0-9A-F \n]+)`", text[match.end():]).group(1)
        expected = bytes.fromhex(value)
        if message == "`00`^0":
            message_cell = ""
        elif message.startswith("ptn("):
            n = 17 ** int(re.match(r"ptn\(17\*\*(\d) bytes\)", message).group(1))
            if n > 17 ** 3:
                continue
            message_cell = f"ptn:{n}"
        else:
            message_cell = bytes.fromhex(message.strip("`")).hex()
        skip = int(length) - 32 if last else 0
        if len(expected) != int(length) - skip:
            raise SystemExit(f"length mismatch at {match.group(0)}")
        rows.append([bits, message_cell, str(int(domain, 16)), str(skip), expected.hex()])
    print(write_module("crypto-hash", "turbo-shake-vectors.nra", "TurboShakeVectors",
                       [f"python3 scripts/crypto-vectors/gen-turboshake.py: RFC 9861 section 5, {URL}."],
                       [("cases", ["bits", "message (hex or ptn:<n>)", "domain byte", "bytes skipped",
                                   "expected output"], rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
