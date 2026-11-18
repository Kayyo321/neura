#!/usr/bin/env python3
"""Build Biscuit v3.0 fixtures from Eclipse Biscuit samples and cryptography 43.

The official sample authorizer source and expected allow/deny outcomes come
from samples/current/samples.json. Signature payload v1 and proof-mismatch
fixtures are generated with independent Python protobuf framing and Ed25519.

    python3 scripts/crypto-vectors/gen-token-biscuit.py [source-directory]
"""

import hashlib
import json
import pathlib
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

from nra_vectors import write_module

PIN = "b3d3fe2d744ea8ee964ab0eba96dbc8c9bde1639"
DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/token/biscuit")
SUPPORTED = {1, 7, 8, 10, 11, 12, 15, 16, 19, 20, 22, 23}
BAD_SIGNATURE = {2, 3, 4, 5, 6}
UNSUPPORTED = {13, 24}
INVALID_RULE = 18


def varint(number: int) -> bytes:
    encoded = bytearray()
    while number >= 128:
        encoded.append((number & 127) | 128)
        number >>= 7
    encoded.append(number)
    return bytes(encoded)


def scalar(field: int, value: int) -> bytes:
    return varint(field * 8) + varint(value)


def blob(field: int, value: bytes) -> bytes:
    return varint(field * 8 + 2) + varint(len(value)) + value


def fields(data: bytes) -> list[tuple[int, int | bytes]]:
    def read_varint(offset: int) -> tuple[int, int]:
        value = 0
        for shift in range(0, 70, 7):
            byte = data[offset]
            offset += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value, offset
        raise ValueError("overlong varint in official fixture")

    offset = 0
    result = []
    while offset < len(data):
        tag, offset = read_varint(offset)
        value, offset = read_varint(offset)
        if tag & 7 == 2:
            end = offset + value
            if end > len(data):
                raise ValueError("truncated bytes in official fixture")
            value = data[offset:end]
            offset = end
        elif tag & 7 != 0:
            raise ValueError("unexpected wire type in official fixture")
        result.append((tag // 8, value))
    return result


def verify_reference(data: bytes, root: bytes) -> None:
    envelope = fields(data)
    key = Ed25519PublicKey.from_public_bytes(root)
    previous = b""
    for _, encoded in (f for f in envelope if f[0] in {2, 3}):
        block = dict(fields(encoded))
        public = dict(fields(block[2]))
        if public[1] != 0:
            raise ValueError("unexpected key algorithm")
        if block.get(5, 0) == 0:
            signed = block[1] + bytes(4) + public[2]
        else:
            if block[5] != 1:
                raise ValueError("unexpected signature payload version")
            signed = tagged_payload(block[1], public[2], previous)
        key.verify(block[3], signed)
        key = Ed25519PublicKey.from_public_bytes(public[2])
        previous = block[3]
    proof = dict(fields(dict(envelope)[4]))
    if 1 in proof:
        if Ed25519PrivateKey.from_private_bytes(proof[1]).public_key().public_bytes_raw() != key.public_bytes_raw():
            raise ValueError("incorrect proof key")
    else:
        key.verify(proof[2], block[1] + bytes(4) + public[2] + previous)


def tagged_payload(data: bytes, public_key: bytes, previous: bytes) -> bytes:
    message = (b"\0BLOCK\0\0VERSION\0" + (1).to_bytes(4, "little") + b"\0PAYLOAD\0" + data
               + b"\0ALGORITHM\0" + bytes(4) + b"\0NEXTKEY\0" + public_key)
    if previous:
        message += b"\0PREVSIG\0" + previous
    return message


def v1_example(root_seed: bytes, authority: bytes) -> tuple[bytes, bytes]:
    """Give a later block a valid right fact absent from the authority."""
    root = Ed25519PrivateKey.from_private_bytes(root_seed)
    next_seed = hashlib.sha256(b"Neura Biscuit test next").digest()
    last_seed = hashlib.sha256(b"Neura Biscuit test last").digest()
    next_key = Ed25519PrivateKey.from_private_bytes(next_seed)
    next_public = next_key.public_key().public_bytes_raw()
    last_public = Ed25519PrivateKey.from_private_bytes(last_seed).public_key().public_bytes_raw()
    raw = dict(fields(authority))[1]
    sig0 = root.sign(tagged_payload(raw, next_public, b""))
    block0 = blob(1, raw) + blob(2, scalar(1, 0) + blob(2, next_public)) + blob(3, sig0) + scalar(5, 1)
    # Authority registered file1/file2. Append file3 and right("file3", "read").
    pred = scalar(1, 4) + blob(2, scalar(3, 1026)) + blob(2, scalar(3, 0))
    local = blob(1, b"file3") + scalar(3, 3) + blob(4, blob(1, pred))
    sig1 = next_key.sign(tagged_payload(local, last_public, sig0))
    block1 = blob(1, local) + blob(2, scalar(1, 0) + blob(2, last_public)) + blob(3, sig1) + scalar(5, 1)
    signed = blob(2, block0) + blob(3, block1)
    return signed + blob(4, blob(1, last_seed)), signed + blob(4, blob(1, bytes(32)))


def main() -> None:
    sources = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    source = json.loads((sources / "samples.json").read_bytes())
    root = bytes.fromhex(source["root_public_key"])
    originals, rejected = [], []
    for sample in source["testcases"]:
        name = sample["filename"]
        index = int(name[4:7])
        if index not in SUPPORTED | BAD_SIGNATURE | UNSUPPORTED | {INVALID_RULE}:
            continue
        encoded = (sources / name).read_bytes()
        if index in SUPPORTED:
            verify_reference(encoded, root)
            for context, validation in sample["validations"].items():
                result = "allow" if "Ok" in validation["result"] else "deny"
                originals.append([name + " " + context, encoded.hex(), validation["authorizer_code"].encode().hex(), result])
        else:
            reason = "unsupported" if index in UNSUPPORTED else ("datalog" if index == INVALID_RULE else "corruption")
            rejected.append([name, encoded.hex(), reason])
    original = (sources / "test001_basic.bc").read_bytes()
    tagged, bad_proof = v1_example(bytes.fromhex(source["root_private_key"]), dict(fields(original))[2])
    verify_reference(tagged, root)
    try:
        verify_reference(bad_proof, root)
    except ValueError:
        pass
    else:
        raise ValueError("wrong proof fixture verified")
    write_module("crypto-token", "biscuit-vectors.nra", "BiscuitVectors", [
        "python3 scripts/crypto-vectors/gen-token-biscuit.py",
        f"Eclipse Biscuit official samples, commit {PIN}.",
        "Signature payload v1 and bad proof use cryptography's Ed25519 implementation.",
    ], [
        ("root", ["root public key"], [[root.hex()]]),
        ("samples", ["official case", "token", "authorizer", "result"], originals),
        ("rejections", ["official case", "token", "reason group"], rejected),
        ("tagged", ["version 1 signed token", "version 1 token with wrong proof"], [[tagged.hex(), bad_proof.hex()]]),
    ])


if __name__ == "__main__":
    main()
