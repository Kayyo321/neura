#!/usr/bin/env python3
"""Generate additional Biscuit v3 tests from official samples and Rust Biscuit 6.0.0.

Pass the pinned official sample directory and the built reference generator.
The second official directory, if given, supplements the primary directory.
"""

import argparse
import json
import pathlib
import runpy
import subprocess
import tempfile

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519, utils

import nra_vectors

PIN = "b3d3fe2d744ea8ee964ab0eba96dbc8c9bde1639"
EXPRESSION_CASES = {9, 13, 14, 17, 27, 28, 31, 32, 33, 34, 38}
SCOPE_CASES = {21, 24, 26, 36, 37}
CHECK_CASES = {25, 29, 30}
EXTERNAL_CASES = {35}
CREDENTIAL_RECORDS = (
    "root.bin", "external.bin", "authority.bc", "attenuated.bc", "request.bin",
    "response.bin", "third-party.bc", "sealed.bc",
    "revocation-0.bin", "revocation-1.bin", "revocation-2.bin",
)
P256_ORDER = int("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551", 16)


def canonical_der(signature: bytes) -> bytes:
    r, s = utils.decode_dss_signature(signature)
    return utils.encode_dss_signature(r, min(s, P256_ORDER - s))


def representation_vectors(reference: pathlib.Path, directory: pathlib.Path) -> list[str]:
    """Issue two synthetic credentials using the standard ECDSA representations."""
    framing = runpy.run_path(str(pathlib.Path(__file__).with_name("gen-token-biscuit.py")))
    blob, scalar = framing["blob"], framing["scalar"]
    root = ec.derive_private_key(int.from_bytes(bytes([8]) * 32, "big"), ec.SECP256R1())
    root_bytes = root.public_key().public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.CompressedPoint,
    )
    seed = bytes([7]) * 32
    next_bytes = ed25519.Ed25519PrivateKey.from_private_bytes(seed).public_key().public_bytes_raw()
    payload = scalar(3, 3)
    transcript = (
        b"\0BLOCK\0\0VERSION\0\x01\0\0\0\0PAYLOAD\0" + payload
        + b"\0ALGORITHM\0\0\0\0\0\0NEXTKEY\0" + next_bytes
    )
    signature = root.sign(transcript, ec.ECDSA(hashes.SHA256(), deterministic_signing=True))
    r, s = utils.decode_dss_signature(signature)
    normalized = canonical_der(signature)
    tokens = []
    for label, value in (("low", min(s, P256_ORDER - s)), ("high", max(s, P256_ORDER - s))):
        signature = utils.encode_dss_signature(r, value)
        block = blob(1, payload) + blob(2, scalar(1, 0) + blob(2, next_bytes)) + blob(3, signature) + scalar(5, 1)
        token = blob(2, block) + blob(4, blob(1, seed))
        name = f"test-p256-{label}.bc"
        (directory / name).write_bytes(token)
        subprocess.run(
            [reference, "check", directory, "p256", root_bytes.hex(), name, "allow if true;", "allow"],
            check=True,
        )
        tokens.append(token.hex())
    return [root_bytes.hex(), *tokens, normalized.hex()]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("reference", type=pathlib.Path)
    parser.add_argument("--supplemental", type=pathlib.Path)
    parser.add_argument("--out-root", type=pathlib.Path)
    args = parser.parse_args()
    if args.out_root:
        nra_vectors.ROOT = args.out_root
    reference = args.reference.resolve()
    corpus = json.loads((args.source / "samples.json").read_bytes())
    root = corpus["root_public_key"]
    columns = ["official case", "token", "authorizer", "result"]
    expressions, scopes, checks, external = [], [], [], []
    for sample in corpus["testcases"]:
        name = sample["filename"]
        index = int(name[4:7])
        if index not in EXPRESSION_CASES | SCOPE_CASES | CHECK_CASES | EXTERNAL_CASES:
            continue
        vector = args.source / name
        if not vector.exists() and args.supplemental:
            vector = args.supplemental / name
        token = vector.read_bytes().hex()
        if index in EXTERNAL_CASES:
            table = external
            command = "check-ffi"
        else:
            table = expressions if index in EXPRESSION_CASES else scopes if index in SCOPE_CASES else checks
            command = "check"
        for label, result in sample["validations"].items():
            code = result["authorizer_code"]
            outcome = "allow" if "Ok" in result["result"] else "deny"
            subprocess.run([reference, command, vector.parent, "ed25519", root, name, code, outcome], check=True)
            expected = "execution" if result["result"].get("Err", {}).get("Execution") else outcome
            table.append([f"{name} {label}".strip(), token, code.encode().hex(), expected])

    with tempfile.TemporaryDirectory() as temporary:
        subprocess.run([reference, "generate", temporary], check=True)
        credential = [(pathlib.Path(temporary) / f"rust-{name}").read_bytes().hex() for name in CREDENTIAL_RECORDS]
        credential.append(canonical_der(bytes.fromhex(credential[8])).hex())
        issued_external = (pathlib.Path(temporary) / "rust-ffi-authority.bc").read_bytes().hex()
        representations = representation_vectors(reference, pathlib.Path(temporary))

    nra_vectors.write_module("crypto-token", "biscuit-v3-vectors.nra", "BiscuitV3Vectors", [
        "python3 scripts/crypto-vectors/gen-token-biscuit-v3.py SOURCE RUST_REFERENCE",
        f"Eclipse Biscuit official samples at commit {PIN}.",
        "Credential records generated with biscuit-auth 6.0.0.",
    ], [
        ("root", ["official Ed25519 public key"], [[root]]),
        ("expressions", columns, expressions),
        ("scopes", columns, scopes),
        ("checks", columns, checks),
        ("external", columns, external),
        ("issued_external", ["P-256 authority with external expressions"], [[issued_external]]),
        ("credentials", [*CREDENTIAL_RECORDS, "canonical authority signature"], [credential]),
        ("representations", ["P-256 public key", "low-S credential", "high-S credential", "stable ID"], [representations]),
    ])


if __name__ == "__main__":
    main()
