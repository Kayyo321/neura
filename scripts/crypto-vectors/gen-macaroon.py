#!/usr/bin/env python3
"""Reproduce binary Macaroon fixtures with pinned libmacaroons and pymacaroons.

Requires PyNaCl 1.5.0 and six 1.16.0 for the pinned pymacaroons checkout.
On the release host, use the already-installed six vendored with distro pip:

    PYTHONPATH=/usr/lib/python3/dist-packages/pip/_vendor/urllib3/packages \
        python3 scripts/crypto-vectors/gen-macaroon.py [reference-repositories-directory]

If six is importable in the Python environment, omit PYTHONPATH.
"""

import argparse
from base64 import urlsafe_b64decode, urlsafe_b64encode
from pathlib import Path
import subprocess
import sys
from unittest.mock import patch

from nra_vectors import write_module

DEFAULT = Path("/home/dev/neura-gate/030/crypto2/sources/token/repos")
LIBMACAROONS = "ca0211d8633c270b73e803cf7c2269f3b81cf530"
PYMACAROONS = "78c55c1d33a0b23ddc71140a9c999f957d79e9dd"


def reference(repos: Path, name: str, commit: str) -> Path:
    path = repos / name
    actual = subprocess.check_output(["git", "-C", path, "rev-parse", "HEAD"], text=True).strip()
    if actual != commit or subprocess.check_output(
            ["git", "-C", path, "status", "--porcelain", "--untracked-files=no"], text=True):
        raise SystemExit(f"{name} must be an unchanged checkout at {commit}")
    return path


def decode(text: str) -> bytes:
    return urlsafe_b64decode(text + "=" * (-len(text) % 4))


def encode(data: bytes) -> str:
    return urlsafe_b64encode(data).decode("ascii").rstrip("=")


def reference_tests(path: Path) -> list[list[str]]:
    rows = []
    for case in sorted((path / "test" / "unit").glob("*.vtest")):
        lines = [line for line in case.read_text().splitlines() if line and not line.startswith("#")]
        assert lines[0] == "version 1" and lines[1] in ("authorized", "unauthorized")
        assert lines[2].startswith("key ")
        token = lines[-1]
        wire_version = "1" if "_v1_" in case.name else "2"
        if wire_version == "1":
            # v1 .vtest inputs wrap the textual wire token in a further base64url layer.
            token = decode(token).decode("ascii")
        predicates = [line.removeprefix("exact ").encode().hex() for line in lines[3:-1]]
        assert len(predicates) <= 2
        predicates += [""] * (2 - len(predicates))
        rows.append([case.stem, wire_version, lines[1], lines[2][4:].encode().hex(), *predicates, token])
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repos", nargs="?", type=Path, default=DEFAULT)
    args = parser.parse_args()
    lib = reference(args.repos, "libmacaroons", LIBMACAROONS)
    py = reference(args.repos, "pymacaroons", PYMACAROONS)
    sys.path.insert(0, str(py))
    from pymacaroons import Macaroon, Verifier
    from pymacaroons.exceptions import MacaroonInvalidSignatureException
    from pymacaroons.macaroon import MACAROON_V1, MACAROON_V2
    from pymacaroons.utils import generate_derived_key, hmac_digest, sign_third_party_caveat

    first = reference_tests(lib)
    discharge = []
    nested = []
    malformed = []
    root_location, root_id, root_key = b"root.example", b"root-id", b"vector-root-key"
    party_location, party_id, party_key = b"auth.example", b"party-id", b"vector-party-key"
    leaf_location, leaf_id, leaf_key = b"nested.example", b"leaf-id", b"vector-nested-key"
    root_condition, party_condition, leaf_condition = b"permission = read", b"role = auditor", b"request = 1"
    nonce, leaf_nonce = bytes(range(24)), bytes(range(24, 48))
    for version in (MACAROON_V1, MACAROON_V2):
        root = Macaroon(location=root_location, identifier=root_id, key=root_key, version=version)
        before_attenuation = root.serialize()
        root.add_first_party_caveat(root_condition)
        with patch("nacl.secret.random", return_value=nonce):
            root.add_third_party_caveat(party_location, party_key, party_id)
        credential = root.serialize()
        party = Macaroon(location=party_location, identifier=party_id, key=party_key, version=version)
        party.add_first_party_caveat(party_condition)
        unbound = party.serialize()
        bound = root.prepare_for_request(party).serialize()


        def verify_with(*supplied):
            verifier = Verifier(discharge_macaroons=list(supplied))
            for predicate in (root_condition, party_condition, leaf_condition):
                verifier.satisfy_exact(predicate)
            return verifier.verify(root, root_key)

        assert verify_with(Macaroon.deserialize(bound))
        try:
            verify_with(Macaroon.deserialize(unbound))
        except MacaroonInvalidSignatureException:
            pass
        else:
            raise AssertionError("reference accepted an unbound discharge")

        discharge.append([str(version),
                          *(arg.hex() for arg in (root_location, root_id, root_key, root_condition,
                                                   party_location, party_id, party_key, party_condition, nonce)),
                          before_attenuation, credential, unbound, bound])

        with patch("nacl.secret.random", return_value=leaf_nonce):
            party.add_third_party_caveat(leaf_location, leaf_key, leaf_id)
        leaf = Macaroon(location=leaf_location, identifier=leaf_id, key=leaf_key, version=version)
        leaf.add_first_party_caveat(leaf_condition)
        bound_party = root.prepare_for_request(party)
        bound_leaf = root.prepare_for_request(leaf)
        assert verify_with(bound_party, bound_leaf)
        cyclic = Macaroon.deserialize(unbound)
        with patch("nacl.secret.random", return_value=leaf_nonce):
            cyclic.add_third_party_caveat(party_location, party_key, party_id)
        nested.append([str(version), bound_party.serialize(), bound_leaf.serialize(), leaf_condition.hex(),
                       root.prepare_for_request(cyclic).serialize()])

        misplaced = Macaroon.deserialize(credential)
        misplaced.caveats.reverse()
        stripped = Macaroon.deserialize(credential)
        stripped.caveats.pop(0)
        wrong_box = Macaroon.deserialize(credential)
        last_caveat = wrong_box.caveats[-1]
        altered_vid = bytearray(last_caveat.verification_key_id)
        altered_vid[24] ^= 1
        last_caveat.verification_key_id = bytes(altered_vid)
        prefix = hmac_digest(hmac_digest(generate_derived_key(root_key), root_id), root_condition)
        wrong_box.signature = sign_third_party_caveat(prefix, bytes(altered_vid), party_id)
        corrupted = bytearray(decode(credential))
        corrupted[-2 if version == MACAROON_V1 else -1] ^= 1
        malformed.extend([
            [str(version), "reordered caveats", "verify", misplaced.serialize()],
            [str(version), "stripped caveat", "verify", stripped.serialize()],
            [str(version), "changed tag", "verify", encode(corrupted)],
            [str(version), "changed secret box tag with valid outer chain", "verify", wrong_box.serialize()],
            [str(version), "extra data", "parse", encode(decode(credential) + b"\0")],
            [str(version), "truncated tag", "parse", encode(decode(credential)[:-1])],
        ])

    tables = [
        ("first", ["case", "version", "result", "key", "first predicate", "second predicate", "token"], first),
        ("third", ["version", "location", "id", "key", "predicate", "party location", "party id",
                   "party key", "party predicate", "nonce", "initial token", "attenuated token",
                   "unbound discharge", "bound discharge"], discharge),
        ("nested", ["version", "bound middle discharge", "bound leaf discharge", "leaf predicate", "cyclic discharge"], nested),
        ("malformed", ["version", "case", "reject at", "token"], malformed),
    ]
    print(write_module("crypto-token", "macaroon-vectors.nra", "MacaroonVectors",
                       ["python3 scripts/crypto-vectors/gen-macaroon.py",
                        f"libmacaroons {LIBMACAROONS} v1/v2 .vtest inputs,",
                        f"pymacaroons {PYMACAROONS} with PyNaCl 1.5.0."], tables))


if __name__ == "__main__":
    main()
