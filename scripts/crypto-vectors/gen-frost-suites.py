#!/usr/bin/env python3
"""Extract RFC 9591 Appendix E.1, E.2, E.4, and E.5 transcripts.

Checks the transcripts with the RFC 8032 point model, cryptography 43, and
Python's hashlib and integer arithmetic. The original E.3 fixture uses
gen-frost.py and libsodium.

    python3 scripts/crypto-vectors/gen-frost-suites.py [rfc9591.txt] [rfc8032.txt]
"""

import hashlib
from pathlib import Path
import re
import sys

from cryptography.hazmat.primitives.asymmetric import ec, ed25519, ed448
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

from nra_vectors import write_module

ROOT = Path("/home/dev/neura-gate/030/crypto2/sources")
RFC9591_SHA = "fd8366795c833348d552e6a4141e8bf413310940fd50af484283207716d1a6a7"
RFC8032_SHA = "ed63657ff389301282b169b0abde9b5dd2c7e4d524fdfa5da6ff3094fc93c4c3"


def read(path: Path, sha: str) -> str:
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != sha:
        raise ValueError(f"unexpected SHA-256: {path}")
    return data.decode("utf-8")


def edwards_model(text: str) -> dict:
    source = text.split("\nimport hashlib;\n", 1)[1].split("\nEd448ph = EdDSA(", 1)[0]
    lines = [line for line in source.splitlines()
             if not line.startswith(("Josefsson & Liusvaara", "RFC 8032 "))]
    model = {}
    # read() pins the whole RFC before extracting and running the reference.
    exec(compile("import hashlib;\n" + "\n".join(lines), "RFC 8032 Appendix A", "exec"), model)
    return model


def fields(text: str, section: int) -> dict[str, bytes]:
    end = "\nAcknowledgments" if section == 5 else f"\nE.{section + 1}. "
    body = text.split(f"\nE.{section}.  FROST", 1)[1].split(end, 1)[0]
    result = {}
    name = None
    for line in body.splitlines():
        match = re.fullmatch(r"   ((?:P[123] )?[a-z_]+(?:\[1\])?): ([0-9a-f]+)", line)
        if match:
            name, result[match[1]] = match.groups()
        elif name is not None and re.fullmatch(r"   [0-9a-f]+", line):
            result[name] += line.strip()
        else:
            name = None
    return {name: bytes.fromhex(value) for name, value in result.items()}


def xmd_sha256(message: bytes, dst: bytes) -> bytes:
    # RFC 9380 expand_message_xmd with a 48-byte output and a short DST.
    domain = dst + bytes([len(dst)])
    initial = hashlib.sha256(bytes(64) + message + b"\x00\x30\x00" + domain).digest()
    first = hashlib.sha256(initial + b"\x01" + domain).digest()
    second = hashlib.sha256(bytes(a ^ b for a, b in zip(initial, first)) + b"\x02" + domain).digest()
    return first + second[:16]


def suite_vectors(table: dict[str, bytes], suite: str, edwards: dict):
    curve = None
    ref_verify = None
    if suite == "Ed25519Sha512":
        context = b"FROST-ED25519-SHA512-v1"
        point = edwards["Edwards25519Point"].stdbase()
        digest = lambda data: hashlib.sha512(data).digest()
        challenge_prefix = b""
        ref_verify = ed25519.Ed25519PublicKey
    elif suite == "Ed448Shake256":
        context = b"FROST-ED448-SHAKE256-v1"
        point = edwards["Edwards448Point"].stdbase()
        digest = lambda data: hashlib.shake_256(data).digest(114)
        challenge_prefix = b"SigEd448\x00\x00"
        ref_verify = ed448.Ed448PublicKey
    elif suite == "P256Sha256":
        context = b"FROST-P256-SHA256-v1"
        curve = ec.SECP256R1()
        modulus = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
    else:
        assert suite == "Secp256k1Sha256"
        context = b"FROST-secp256k1-SHA256-v1"
        curve = ec.SECP256K1()
        modulus = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
    scalar_length = 57 if suite == "Ed448Shake256" else 32
    if curve is None:
        modulus = point.l()
        byteorder = "little"

        def base(scalar: int) -> bytes:
            return bytes((point * scalar).encode())

        torsion_encoding = bytes(point.base_field.make(-1).tobytes(8 * scalar_length))
        torsion = point.decode(torsion_encoding)
        mixed = point + torsion
        assert mixed * modulus != point.zero_elem()
        assert mixed * (1 << point.c()) != point.zero_elem()
        inadmissible_points = [bytes(point.zero_elem().encode()), torsion_encoding, bytes(mixed.encode())]
    else:
        byteorder = "big"
        digest = lambda data: hashlib.sha256(data).digest()

        def base(scalar: int) -> bytes:
            return ec.derive_private_key(scalar, curve).public_key().public_bytes(
                Encoding.X962, PublicFormat.CompressedPoint)

        uncompressed = ec.derive_private_key(1, curve).public_key().public_bytes(
            Encoding.X962, PublicFormat.UncompressedPoint)
        inadmissible_points = [bytes(33), bytes([2]) + bytes([255]) * 32, uncompressed]

    def pack(value: int) -> bytes:
        return (value % modulus).to_bytes(scalar_length, byteorder)

    def unpack(value: bytes) -> int:
        return int.from_bytes(value, byteorder)

    def h(label: bytes, data: bytes) -> bytes:
        return digest(context + label + data)

    def hscalar(label: bytes, data: bytes) -> int:
        if curve:
            uniform = xmd_sha256(data, context + label)
        elif label == b"chal":
            uniform = digest(challenge_prefix + data)
        else:
            uniform = h(label, data)
        return unpack(uniform) % modulus

    secret = table["group_secret_key"]
    slope = table["share_polynomial_coefficients[1]"]
    message = table["message"]
    public_key = table["group_public_key"]
    assert base(unpack(secret)) == public_key
    members = []
    for i in (1, 2, 3):
        share = table[f"P{i} participant_share"]
        assert pack(unpack(secret) + i * unpack(slope)) == share
        members.extend([pack(i), share, base(unpack(share))])

    commitments, state = [], []
    for i in (1, 3):
        prefix = f"P{i} "
        sk = table[prefix + "participant_share"]
        rh = table[prefix + "hiding_nonce_randomness"]
        rb = table[prefix + "binding_nonce_randomness"]
        d, e = hscalar(b"nonce", rh + sk), hscalar(b"nonce", rb + sk)
        assert pack(d) == table[prefix + "hiding_nonce"]
        assert pack(e) == table[prefix + "binding_nonce"]
        public_d, public_e = base(d), base(e)
        assert public_d == table[prefix + "hiding_nonce_commitment"]
        assert public_e == table[prefix + "binding_nonce_commitment"]
        wire = pack(i) + public_d + public_e
        commitments.append(wire)
        state.append((i, unpack(sk), d, e, rh + rb, wire))

    prefix = public_key + h(b"msg", message) + h(b"com", b"".join(commitments))
    rho = [hscalar(b"rho", prefix + pack(i)) for i in (1, 3)]
    for i, scalar in zip((1, 3), rho):
        assert prefix + pack(i) == table[f"P{i} binding_factor_input"]
        assert pack(scalar) == table[f"P{i} binding_factor"]
    group_nonce = sum(d + factor * e for (_, _, d, e, _, _), factor in zip(state, rho)) % modulus
    aggregate_commitment = base(group_nonce)
    challenge = hscalar(b"chal", aggregate_commitment + public_key + message)
    round_rows = []
    scalars = []
    for (i, sk, d, e, randomness, wire), factor in zip(state, rho):
        other = 4 - i
        lagrange = other * pow(other - i, -1, modulus) % modulus
        sig_share = (d + e * factor + sk * lagrange * challenge) % modulus
        assert pack(sig_share) == table[f"P{i} sig_share"]
        scalars.append(sig_share)
        round_rows.extend([randomness, wire, pack(sig_share)])
    total = sum(scalars) % modulus
    signature = aggregate_commitment + pack(total)
    assert total == (group_nonce + challenge * unpack(secret)) % modulus
    assert signature == table["sig"]
    if ref_verify is not None:
        ref_verify.from_public_bytes(public_key).verify(signature, message)

    invalid_scalar = modulus.to_bytes(scalar_length, byteorder)
    bad_signature = aggregate_commitment + pack(total + 1)
    if suite == "Ed448Shake256":
        # A high byte that would alias the canonical value if it were truncated.
        bad_top_byte = signature[:-1] + b"\x01"
    else:
        bad_top_byte = signature + b"\x00"
    vectors = [secret, slope, public_key, message, aggregate_commitment, signature,
               *members, *round_rows, *inadmissible_points, invalid_scalar, bad_signature, bad_top_byte]
    return [[cell.hex() for cell in vectors]]


def main() -> None:
    rfc9591 = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "advanced/rfc9591.txt"
    rfc8032 = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "asymmetric/rfc8032.txt"
    text = read(rfc9591, RFC9591_SHA)
    edwards = edwards_model(read(rfc8032, RFC8032_SHA))
    suites = [(1, "Ed25519Sha512"), (2, "Ed448Shake256"), (4, "P256Sha256"), (5, "Secp256k1Sha256")]
    columns = ["secret", "slope", "public key", "message", "group commitment", "signature",
               "P1 identifier", "P1 share", "P1 verification key",
               "P2 identifier", "P2 share", "P2 verification key",
               "P3 identifier", "P3 share", "P3 verification key",
               "P1 randomness", "P1 commitment", "P1 signature share",
               "P3 randomness", "P3 commitment", "P3 signature share",
               "invalid point 1", "invalid point 2", "invalid point 3",
               "out-of-range scalar", "modified signature", "noncanonical length or high byte"]
    print(write_module(
        "crypto-advanced", "frost-suite-vectors.nra", "FrostSuiteVectors",
        ["python3 scripts/crypto-vectors/gen-frost-suites.py",
         "RFC 9591 Appendix E.1, E.2, E.4, E.5. Independent RFC 8032 and cryptography checks.",
         f"RFC 9591 SHA-256: {RFC9591_SHA}. RFC 8032 SHA-256: {RFC8032_SHA}."],
        [(f"suite_{i}", columns, suite_vectors(fields(text, i), name, edwards)) for i, name in suites]))


if __name__ == "__main__":
    main()
