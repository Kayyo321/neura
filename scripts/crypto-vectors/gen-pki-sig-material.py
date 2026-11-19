#!/usr/bin/env python3
"""Records the openssl material behind the signature tests of Std.Crypto.Pki (AlgorithmVerifier).

Runs the `openssl` 3.5.7 command line to make one key per algorithm and self-signed certificates, one per
signature algorithm, under the recorded-material directory (default
/home/dev/neura-gate/030/crypto2/sources/pki2/material, or the first argument). Keys and certificates are made
once and kept: RSA-PSS, ML-DSA (hedged) and SLH-DSA (randomized) signatures differ on every run, so the material
directory is the record and gen-pki-signatures.py only transcodes and checks it. Delete a file to make it again.

This is a separate directory from gen-pki-material.py's (phase 1), so re-running neither changes the other's
vector files.

    python3 scripts/crypto-vectors/gen-pki-sig-material.py [material-dir]
"""

import pathlib
import subprocess
import sys

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/pki2/material")

# Key name -> genpkey arguments.
KEYS = {
    "rsa2048": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048"],
    "rsa3072": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072"],
    "rsapss-sha256": ["-algorithm", "RSA-PSS", "-pkeyopt", "rsa_keygen_bits:2048", "-pkeyopt",
                      "rsa_pss_keygen_md:sha256", "-pkeyopt", "rsa_pss_keygen_mgf1_md:sha256", "-pkeyopt",
                      "rsa_pss_keygen_saltlen:32"],
    "rsapss-open": ["-algorithm", "RSA-PSS", "-pkeyopt", "rsa_keygen_bits:2048"],
    "p256": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256"],
    "p384": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-384"],
    "p521": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-521"],
    "secp256k1": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:secp256k1"],
    "ed25519": ["-algorithm", "ED25519"],
    "ed448": ["-algorithm", "ED448"],
    "mldsa44": ["-algorithm", "ML-DSA-44"],
    "mldsa65": ["-algorithm", "ML-DSA-65"],
    "mldsa87": ["-algorithm", "ML-DSA-87"],
}
SLH_SETS = ["SHA2-128s", "SHA2-128f", "SHA2-192s", "SHA2-192f", "SHA2-256s", "SHA2-256f",
            "SHAKE-128s", "SHAKE-128f", "SHAKE-192s", "SHAKE-192f", "SHAKE-256s", "SHAKE-256f"]
for s in SLH_SETS:
    KEYS["slh-" + s.lower()] = ["-algorithm", "SLH-DSA-" + s]

ECDSA = ["-sigopt", "nonce-type:1"]


def pss(md, salt, mgf=None):
    return ["-" + md, "-sigopt", "rsa_padding_mode:pss", "-sigopt", f"rsa_pss_saltlen:{salt}", "-sigopt",
            f"rsa_mgf1_md:{mgf or md}"]


# Certificate name -> (key, extra req arguments). Every certificate is self-signed.
CERTS = {
    "rsa-sha224": ("rsa2048", ["-sha224"]),
    "rsa-sha256": ("rsa2048", ["-sha256"]),
    "rsa-sha384": ("rsa2048", ["-sha384"]),
    "rsa-sha512": ("rsa2048", ["-sha512"]),
    "rsa-sha3-224": ("rsa2048", ["-sha3-224"]),
    "rsa-sha3-256": ("rsa2048", ["-sha3-256"]),
    "rsa-sha3-384": ("rsa2048", ["-sha3-384"]),
    "rsa-sha3-512": ("rsa2048", ["-sha3-512"]),
    "rsa3072-sha256": ("rsa3072", ["-sha256"]),
    "rsa-sha1": ("rsa2048", ["-sha1"]),
    "pss-sha256-32": ("rsa2048", pss("sha256", 32)),
    "pss-sha384-48": ("rsa2048", pss("sha384", 48)),
    "pss-sha512-64": ("rsa2048", pss("sha512", 64)),
    "pss-sha256-0": ("rsa2048", pss("sha256", 0)),
    "pss-sha256-mgf384": ("rsa2048", pss("sha256", 32, "sha384")),
    "pss-sha1-20": ("rsa2048", pss("sha1", 20)),
    "psskey-sha256-32": ("rsapss-sha256", pss("sha256", 32)),
    "psskey-sha256-40": ("rsapss-sha256", pss("sha256", 40)),
    "psskeyopen-sha384-48": ("rsapss-open", pss("sha384", 48)),
    "p256-sha256": ("p256", ECDSA + ["-sha256"]),
    "p256-sha224": ("p256", ECDSA + ["-sha224"]),
    "p256-sha384": ("p256", ECDSA + ["-sha384"]),
    "p256-sha512": ("p256", ECDSA + ["-sha512"]),
    "p256-sha3-224": ("p256", ECDSA + ["-sha3-224"]),
    "p256-sha3-256": ("p256", ECDSA + ["-sha3-256"]),
    "p384-sha384": ("p384", ECDSA + ["-sha384"]),
    "p384-sha3-384": ("p384", ECDSA + ["-sha3-384"]),
    "p521-sha512": ("p521", ECDSA + ["-sha512"]),
    "p521-sha3-512": ("p521", ECDSA + ["-sha3-512"]),
    "secp256k1-sha256": ("secp256k1", ECDSA + ["-sha256"]),
    "ed25519": ("ed25519", []),
    "ed448": ("ed448", []),
    "mldsa44": ("mldsa44", []),
    "mldsa65": ("mldsa65", []),
    "mldsa87": ("mldsa87", []),
}
for s in SLH_SETS:
    CERTS["slh-" + s.lower()] = ("slh-" + s.lower(), [])

CONFIG = """
[req]
distinguished_name = dn
[dn]
[ca]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign, cRLSign, digitalSignature
subjectKeyIdentifier = hash
"""


def run(args, cwd):
    subprocess.run(["openssl"] + args, cwd=cwd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main() -> int:
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    keys = out / "keys"
    certs = out / "certs"
    keys.mkdir(parents=True, exist_ok=True)
    certs.mkdir(parents=True, exist_ok=True)
    (out / "openssl.cnf").write_text(CONFIG, encoding="utf-8")
    for name, args in KEYS.items():
        path = keys / f"{name}.pem"
        if not path.exists():
            run(["genpkey"] + args + ["-out", str(path)], out)
    for serial, (name, (key, extra)) in enumerate(CERTS.items(), start=1):
        path = certs / f"{name}.der"
        if path.exists():
            continue
        run(["req", "-new", "-x509", "-config", str(out / "openssl.cnf"), "-extensions", "ca", "-key",
             str(keys / f"{key}.pem"), "-subj", f"/O=Neura Test/CN={name}", "-set_serial", str(serial),
             "-not_before", "20260101000000Z", "-not_after", "20360101000000Z", "-outform", "DER", "-out", str(path)]
            + extra, out)
    print(len(CERTS), "certificates in", certs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
