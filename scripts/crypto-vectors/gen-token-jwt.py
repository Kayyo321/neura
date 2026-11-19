#!/usr/bin/env python3
"""Extract the authenticated JWT payload from RFC 7519 section 3.1.

    python3 scripts/crypto-vectors/gen-token-jwt.py [rfc7519.txt]
"""

import json
import pathlib
import re
import sys

from nra_vectors import write_module

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/token/rfc7519.txt")


def main() -> None:
    source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    text = source.read_text(encoding="utf-8")
    marker = "in this example for the JWT Claims Set above, is the JWS Payload:"
    _, octets = text.split(marker, 1)
    match = re.match(r"\s*(\[[0-9,\s]+\])", octets)
    if match is None:
        raise ValueError("RFC 7519 section 3.1 payload bytes missing")
    payload = bytes(json.loads(match[1]))
    claims = json.loads(payload)
    print(write_module("crypto-token", "jwt-vectors.nra", "JwtVectors", [
        "python3 scripts/crypto-vectors/gen-token-jwt.py",
        "RFC 7519 section 3.1, https://www.rfc-editor.org/rfc/rfc7519.txt",
    ], [
        ("example", ["JWT payload bytes", "exp", "iss"], [[payload.hex(), str(claims["exp"]), claims["iss"]]]),
    ]))


if __name__ == "__main__":
    main()
