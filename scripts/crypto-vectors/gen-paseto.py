#!/usr/bin/env python3
"""Reproduce the PASETO v4 fixtures from the pinned official v4.json.

    python3 scripts/crypto-vectors/gen-paseto.py [path/to/paseto-v4.json]
"""

import argparse
import hashlib
import json
from pathlib import Path

from nra_vectors import write_module

COMMIT = "32d7406591eb022f9eff88abb84106dd9d42c0f2"
SHA256 = "0b72948b65d1f73f574c9ad2aa3481ec27bf8c632f5f6e1596cd41f5b9703387"
DEFAULT = Path("/home/dev/neura-gate/030/crypto2/sources/token/paseto-v4.json")


def text_bytes(case: dict, name: str) -> str:
    return case[name].encode("utf-8").hex()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vectors", nargs="?", type=Path, default=DEFAULT)
    args = parser.parse_args()
    source = args.vectors.read_bytes()
    if hashlib.sha256(source).hexdigest() != SHA256:
        raise SystemExit(f"unexpected SHA-256 for {args.vectors}")

    local, public, reject = [], [], []
    for case in json.loads(source)["tests"]:
        name, token = case["name"], case["token"]
        assertion = text_bytes(case, "implicit-assertion")
        if case["expect-fail"]:
            purpose = "local" if "key" in case else "public"
            key = case["key"] if purpose == "local" else case["public-key"]
            reject.append([name, purpose, key, assertion, token])
            continue
        footer, payload = text_bytes(case, "footer"), text_bytes(case, "payload")
        if "key" in case:
            local.append([name, case["key"], case["nonce"], payload, footer, assertion, token])
        else:
            public.append([name, case["secret-key-seed"], case["public-key"], payload, footer, assertion, token])

    print(write_module("crypto-token", "paseto-vectors.nra", "PasetoVectors",
                       ["python3 scripts/crypto-vectors/gen-paseto.py",
                        f"paseto-standard/test-vectors at {COMMIT}, v4.json."],
                       [("local", ["name", "key", "nonce", "payload", "footer", "assertion", "token"], local),
                        ("public_tokens", ["name", "seed", "public key", "payload", "footer", "assertion", "token"], public),
                        ("reject", ["name", "purpose", "key", "assertion", "token"], reject)]))


if __name__ == "__main__":
    main()
