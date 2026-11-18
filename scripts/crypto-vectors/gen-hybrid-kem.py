#!/usr/bin/env python3
"""Known answers for the TLS X25519MLKEM768 group and X-Wing.

    python3 scripts/crypto-vectors/gen-hybrid-kem.py [x-wing-draft.txt]

X-Wing inputs and outputs are Appendix C of draft-connolly-cfrg-xwing-kem-11.
Reuse those inputs for the TLS construction of draft-ietf-tls-ecdhe-mlkem-05,
section 4. OpenSSL 3.5.7 supplies ML-KEM-768 and cryptography 43.0.0 supplies
X25519. Both calculate the component outputs independently of Neura. Python
hashlib supplies SHAKE256 and SHA3-256, including for implicit rejection and
the X-Wing case with an all-zero X25519 ciphertext.
"""

import hashlib
import pathlib
import re
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey

from nra_vectors import write_module

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/hybrid/draft-connolly-cfrg-xwing-kem-11.txt")
DRAFT_SHA256 = "1353f61f0d706199d86b6802eab83cabbb5195b2d99fb35bca20f5e4f83db786"
X_WING_LABEL = bytes.fromhex("5c2e2f2f5e5c")


def appendix(text):
    start = text.index("\nAppendix C.")
    end = text.index("\nAppendix D.", start)
    cases = []
    label = None
    for line in text[start:end].splitlines():
        match = re.fullmatch(r"(seed|sk|pk|eseed|ct|ss)(?:\s+([a-f0-9]+))?", line.strip())
        if match:
            label = match[1]
            if label == "seed":
                cases.append({})
            cases[-1][label] = match[2] or ""
        elif label and re.fullmatch(r"[a-f0-9]+", line.strip()):
            cases[-1][label] += line.strip()
    if len(cases) != 3:
        raise ValueError("expected three X-Wing vectors")
    for case in cases:
        decoded = {key: bytes.fromhex(value) for key, value in case.items()}
        if list(map(len, (decoded[key] for key in ("seed", "sk", "pk", "eseed", "ct", "ss")))) != [
                32, 32, 1216, 64, 1120, 32]:
            raise ValueError("unexpected X-Wing vector lengths")
        if decoded["seed"] != decoded["sk"]:
            raise ValueError("unexpected X-Wing seed encoding")
        yield decoded


def openssl(root, *args):
    return subprocess.run(["openssl", *args], cwd=root, check=True, capture_output=True).stdout


def reference(case, root):
    expanded = hashlib.shake_256(case["sk"]).digest(96)
    recipient = X25519PrivateKey.from_private_bytes(expanded[64:])
    ephemeral = X25519PrivateKey.from_private_bytes(case["eseed"][32:])
    pk_x = recipient.public_key().public_bytes_raw()
    ct_x = ephemeral.public_key().public_bytes_raw()
    secret_x = ephemeral.exchange(recipient.public_key())

    openssl(root, "genpkey", "-algorithm", "ML-KEM-768", "-pkeyopt", "hexseed:" + expanded[:64].hex(),
            "-out", "sk.pem")
    openssl(root, "pkey", "-in", "sk.pem", "-pubout", "-out", "pk.pem")
    public_der = openssl(root, "pkey", "-pubin", "-in", "pk.pem", "-pubout", "-outform", "DER")
    expected_spki_prefix = bytes.fromhex("308204b2300b0609608648016503040402038204a100")
    if public_der != expected_spki_prefix + case["pk"][:1184] or pk_x != case["pk"][1184:]:
        raise ValueError("draft and reference public keys differ")
    openssl(root, "pkeyutl", "-encap", "-pubin", "-inkey", "pk.pem", "-out", "ct",
            "-secret", "secret", "-pkeyopt", "hexikme:" + case["eseed"][:32].hex())
    ct_ml = (root / "ct").read_bytes()
    secret_ml = (root / "secret").read_bytes()
    if ct_ml + ct_x != case["ct"] or len(secret_ml) != 32:
        raise ValueError("draft and reference encapsulations differ")

    if hashlib.sha3_256(secret_ml + secret_x + ct_x + pk_x + X_WING_LABEL).digest() != case["ss"]:
        raise ValueError("draft and reference shared secrets differ")

    altered_ml = bytes([ct_ml[0] ^ 1]) + ct_ml[1:]
    (root / "ct").write_bytes(altered_ml)
    implicit = openssl(root, "pkeyutl", "-decap", "-inkey", "sk.pem", "-in", "ct")
    if implicit != hashlib.shake_256(expanded[32:64] + altered_ml).digest(32):
        raise ValueError("reference did not implicitly reject the modified ciphertext")
    x_wing_implicit = hashlib.sha3_256(implicit + secret_x + ct_x + pk_x + X_WING_LABEL).digest()
    x_wing_low_order = hashlib.sha3_256(secret_ml + bytes(64) + pk_x + X_WING_LABEL).digest()
    return [expanded, secret_ml + secret_x, implicit + secret_x, x_wing_implicit, x_wing_low_order]


def main():
    draft = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    data = draft.read_bytes()
    if hashlib.sha256(data).hexdigest() != DRAFT_SHA256:
        raise SystemExit(f"unexpected SHA-256 for {draft}")

    rows = []
    with tempfile.TemporaryDirectory() as directory:
        for case in appendix(data.decode("utf-8")):
            values = [case[key] for key in ("sk", "pk", "eseed", "ct", "ss")]
            values.extend(reference(case, pathlib.Path(directory)))
            rows.append([value.hex() for value in values])

    header = [
        "python3 scripts/crypto-vectors/gen-hybrid-kem.py",
        "X-Wing draft-connolly-cfrg-xwing-kem-11, Appendix C, SHA-256 " + DRAFT_SHA256 + ".",
        "TLS draft-ietf-tls-ecdhe-mlkem-05 section 4: OpenSSL 3.5.7 ML-KEM-768 and cryptography 43.0.0 X25519.",
        "Negative cases: changed ML-KEM ciphertext and all-zero X25519 ciphertext.",
    ]
    columns = ["X-Wing seed", "public key", "encapsulation seed", "ciphertext", "X-Wing secret",
               "TLS seed", "TLS secret", "TLS implicit rejection secret", "X-Wing implicit rejection secret",
               "X-Wing low-order secret"]
    print(write_module("crypto-hybrid", "hybrid-kem-vectors.nra", "HybridKemVectors", header,
                       [("cases", columns, rows)]))


if __name__ == "__main__":
    main()
