#!/usr/bin/env python3
"""Extract and verify the RFC 9591 Appendix E.3 ristretto255 transcript.

Needs libsodium with ristretto255 support. Usage: python3 scripts/crypto-vectors/gen-frost.py [rfc9591.txt]
"""

import argparse
from ctypes import CDLL, c_char_p, c_int, c_void_p, create_string_buffer
from ctypes.util import find_library
from hashlib import sha256, sha512
from pathlib import Path
import re

from nra_vectors import write_module

DEFAULT = Path("/home/dev/neura-gate/030/crypto2/sources/advanced/rfc9591.txt")
DIGEST = "fd8366795c833348d552e6a4141e8bf413310940fd50af484283207716d1a6a7"
ORDER = 2**252 + 27742317777372353535851937790883648493
CONTEXT = b"FROST-RISTRETTO255-SHA512-v1"


def fields(text: str) -> dict[str, bytes]:
    body = text.split("\nE.3.  FROST(ristretto255, SHA-512)\n", 1)[1].split("\nE.4.", 1)[0]
    result = {}
    name = None
    for line in body.splitlines():
        match = re.fullmatch(r"   ((?:P[123] )?[a-z_]+(?:\[1\])?): ([0-9a-f]+)", line)
        if match:
            name = match[1]
            result[name] = match[2]
        elif name is not None and re.fullmatch(r"   [0-9a-f]+", line):
            result[name] += line.strip()
        else:
            name = None
    return {name: bytes.fromhex(hexadecimal) for name, hexadecimal in result.items()}


def scalar(value: int) -> bytes:
    return (value % ORDER).to_bytes(32, "little")


def number(encoded: bytes) -> int:
    return int.from_bytes(encoded, "little")


def h(label: bytes, message: bytes) -> bytes:
    return sha512(CONTEXT + label + message).digest()


def hscalar(label: bytes, message: bytes) -> int:
    return number(h(label, message)) % ORDER


def sodium_function(lib, name: str, inputs: int):
    func = getattr(lib, name)
    func.argtypes = [c_void_p] + [c_char_p] * inputs
    func.restype = c_int

    def call(*args: bytes) -> bytes:
        out = create_string_buffer(32)
        status = func(out, *args)
        if status != 0:
            raise ValueError(f"{name} rejected its inputs")
        return out.raw

    return call


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rfc", nargs="?", type=Path, default=DEFAULT)
    args = parser.parse_args()
    rfc = args.rfc.read_bytes()
    if sha256(rfc).hexdigest() != DIGEST:
        raise SystemExit(f"unexpected RFC 9591 SHA-256 for {args.rfc}")
    vector = fields(rfc.decode("utf-8"))

    libsodium = find_library("sodium")
    if not libsodium:
        raise SystemExit("libsodium with ristretto255 is required")
    lib = CDLL(libsodium)
    base = sodium_function(lib, "crypto_scalarmult_ristretto255_base", 1)
    multiply = sodium_function(lib, "crypto_scalarmult_ristretto255", 2)
    add = sodium_function(lib, "crypto_core_ristretto255_add", 2)

    secret, slope = vector["group_secret_key"], vector["share_polynomial_coefficients[1]"]
    group_key, message = vector["group_public_key"], vector["message"]
    assert base(secret) == group_key
    members = []
    for idx in (1, 2, 3):
        share = vector[f"P{idx} participant_share"]
        assert scalar(number(secret) + idx * number(slope)) == share
        members.append([scalar(idx).hex(), share.hex(), base(share).hex()])

    pair = [1, 3]
    transcript = []
    entries = []
    signer_state = []
    for idx in pair:
        sk = vector[f"P{idx} participant_share"]
        hide_random = vector[f"P{idx} hiding_nonce_randomness"]
        bind_random = vector[f"P{idx} binding_nonce_randomness"]
        d, e = hscalar(b"nonce", hide_random + sk), hscalar(b"nonce", bind_random + sk)
        assert scalar(d) == vector[f"P{idx} hiding_nonce"]
        assert scalar(e) == vector[f"P{idx} binding_nonce"]
        public_d, public_e = base(scalar(d)), base(scalar(e))
        assert vector[f"P{idx} hiding_nonce_commitment"] == public_d
        assert vector[f"P{idx} binding_nonce_commitment"] == public_e
        entry = scalar(idx) + public_d + public_e
        entries.append(entry)
        signer_state.append((sk, d, e, public_d, public_e))
        transcript.append([scalar(idx).hex(), (hide_random + bind_random).hex(), entry.hex(),
                           vector[f"P{idx} sig_share"].hex()])

    prefix = group_key + h(b"msg", message) + h(b"com", b"".join(entries))
    rho = [hscalar(b"rho", prefix + scalar(idx)) for idx in pair]
    for idx, factor in zip(pair, rho):
        assert scalar(factor) == vector[f"P{idx} binding_factor"]
        assert prefix + scalar(idx) == vector[f"P{idx} binding_factor_input"]
    comm_parts = [add(st[3], multiply(scalar(r), st[4])) for st, r in zip(signer_state, rho)]
    commitment = add(*comm_parts)
    challenge = hscalar(b"chal", commitment + group_key + message)
    partials = []
    for i, idx in enumerate(pair):
        other = pair[1 - i]
        coefficient = other * pow(other - idx, -1, ORDER) % ORDER
        sk, d, e, _, _ = signer_state[i]
        z = scalar(d + e * rho[i] + challenge * coefficient * number(sk))
        assert z == vector[f"P{idx} sig_share"]
        partials.append(number(z))
    final_scalar = scalar(sum(partials))
    assert commitment + final_scalar == vector["sig"]
    assert base(final_scalar) == add(commitment, multiply(scalar(challenge), group_key))

    bad_commit = [
        ["zero identifier", (bytes(32) + entries[0][32:]).hex()],
        ["noncanonical identifier", (ORDER.to_bytes(32, "little") + entries[0][32:]).hex()],
        ["identity hiding commitment", (entries[0][:32] + bytes(32) + entries[0][64:]).hex()],
        ["noncanonical binding commitment", (entries[0][:64] + bytes([255]) * 32).hex()],
        ["truncated commitment", entries[0][:-1].hex()],
    ]
    tampered = scalar(number(vector["P1 sig_share"]) + 1)
    bad_signature = [
        ["identity signature commitment", (bytes(32) + final_scalar).hex()],
        ["noncanonical signature scalar", (commitment + ORDER.to_bytes(32, "little")).hex()],
        ["different signature scalar", (commitment + scalar(number(final_scalar) + 1)).hex()],
    ]
    main_values = [part.hex() for part in (secret, slope, group_key, message, commitment, vector["sig"])]
    mutation = [
        (scalar(4) + entries[1][32:]).hex(), (scalar(2) + entries[1][32:]).hex(),
        (scalar(1) + entries[1][32:]).hex(), tampered.hex(), ORDER.to_bytes(32, "little").hex(),
        bytes(32).hex(), (bytes(31) + b"\x10").hex(),
    ]
    print(write_module("crypto-advanced", "frost-vectors.nra", "FrostVectors",
                       ["python3 scripts/crypto-vectors/gen-frost.py",
                        f"RFC 9591 Appendix E.3. SHA-256 of source: {DIGEST}.",
                        "Points and equations independently checked through libsodium ristretto255."],
                       [("group", ["secret", "slope", "public key", "message", "group commitment", "signature"],
                         [main_values]),
                        ("members", ["identifier", "secret share", "verification key"], members),
                        ("transcript", ["identifier", "round one randomness", "commitment entry", "signature share"],
                         transcript),
                        ("bad_commitment", ["case", "bytes"], bad_commit),
                        ("bad_signature", ["case", "bytes"], bad_signature),
                        ("mutation", ["unknown commitment", "signer 2 commitment", "substituted signer 1 commitment",
                                      "wrong signature share", "noncanonical scalar", "zero scalar", "large identifier"],
                         [mutation])]))


if __name__ == "__main__":
    main()
