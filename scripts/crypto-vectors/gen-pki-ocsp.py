#!/usr/bin/env python3
"""OCSP vectors for Std.Crypto.Pki.Ocsp.

The requests and responses are the `openssl ocsp` 3.5.7 records that gen-pki-material.py keeps under
/home/dev/neura-gate/030/crypto2/sources/pki/material/ocsp (responses carry the time they were made, so they are
recorded once). Expected request fields come from `openssl ocsp -req_text`, response fields from Python
`cryptography` (version in the output); the CertID hashes
of extra issuers are computed with hashlib (SHA-1, SHA-256); the signature truth table (SHA-256 of signer SPKI ||
signed bytes || signature -> valid/invalid) is computed with `cryptography`.

    python3 scripts/crypto-vectors/gen-pki-ocsp.py [material-dir]
"""

import calendar
import hashlib
import pathlib
import sys

import cryptography
from cryptography import x509
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509 import ocsp

from nra_vectors import write_module

MATERIAL = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/pki/material")


def epoch(dt) -> str:
    return "" if dt is None else str(calendar.timegm(dt.timetuple()))


def spki(cert: x509.Certificate) -> bytes:
    return cert.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)


def key_bits(cert: x509.Certificate) -> bytes:
    """The subjectPublicKey BIT STRING value: the X9.62 point of an EC key, the RSAPublicKey DER of an RSA key."""
    key = cert.public_key()
    if isinstance(key, ec.EllipticCurvePublicKey):
        return key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    return key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.PKCS1)


def request_fields(der: bytes):
    """(hash, name hash, key hash, serial, count) of the first CertID, from `openssl ocsp -req_text`."""
    import subprocess
    text = subprocess.run(["openssl", "ocsp", "-reqin", "/dev/stdin", "-req_text", "-noverify"], input=der,
                          capture_output=True, check=True).stdout.decode()
    lines = [line.strip() for line in text.splitlines()]
    def first(prefix):
        return next(line.split(":", 1)[1].strip() for line in lines if line.startswith(prefix))
    count = sum(1 for line in lines if line.startswith("Certificate ID:"))
    return first("Hash Algorithm"), first("Issuer Name Hash").lower(), first("Issuer Key Hash").lower(), \
        first("Serial Number").lower(), count


def verify(key_cert: x509.Certificate, data: bytes, signature: bytes, hash_alg) -> bool:
    key = key_cert.public_key()
    try:
        key.verify(signature, data, ec.ECDSA(hash_alg))
        return True
    except InvalidSignature:
        return False


def main() -> int:
    material = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else MATERIAL
    certs = {}
    for name in ["root", "inter", "leaf", "neg-expired", "neg-wildcard", "rsa-root"]:
        certs[name] = (material / "certs" / f"{name}.der").read_bytes()
    for name in ["ocsp-responder", "ocsp-no-eku"]:
        certs[name] = (material / "ocsp" / f"{name}.der").read_bytes()
    loaded = {n: x509.load_der_x509_certificate(d) for n, d in certs.items()}
    request_rows = []
    for name in ["req-sha1-nonce", "req-sha256", "req-revoked", "req-unknown", "req-two"]:
        der = (material / "ocsp" / f"{name}.der").read_bytes()
        hash_name, name_hash, key_hash, serial, count = request_fields(der)
        nonce = ""
        if count == 1:
            for ext in ocsp.load_der_ocsp_request(der).extensions:
                if isinstance(ext.value, x509.OCSPNonce):
                    nonce = ext.value.nonce.hex()
        request_rows.append([name, der.hex(), hash_name, name_hash, key_hash, serial, nonce, str(count)])
    response_rows = []
    table = {}
    for name in ["resp-good-direct", "resp-good-nonce", "resp-revoked", "resp-unknown", "resp-two", "resp-delegated",
                 "resp-delegated-bykey", "resp-no-eku"]:
        der = (material / "ocsp" / f"{name}.der").read_bytes()
        resp = ocsp.load_der_ocsp_response(der)
        responder = resp.responder_name.rfc4514_string() if resp.responder_name is not None else resp.responder_key_hash.hex()
        singles = []
        for single in resp.responses:
            status = {ocsp.OCSPCertStatus.GOOD: "good", ocsp.OCSPCertStatus.REVOKED: "revoked",
                      ocsp.OCSPCertStatus.UNKNOWN: "unknown"}[single.certificate_status]
            reason = "" if single.revocation_reason is None else single.revocation_reason.name
            singles.append(f"{format(single.serial_number, 'x')};{status};{epoch(single.this_update_utc)};"
                           f"{epoch(single.next_update_utc)};{epoch(single.revocation_time_utc)};{reason}")
        nonce = ""
        for ext in resp.extensions:
            if isinstance(ext.value, x509.OCSPNonce):
                nonce = ext.value.nonce.hex()
        response_rows.append([name, der.hex(), responder, epoch(resp.produced_at_utc), "|".join(singles), nonce,
                              str(len(resp.certificates))])
        # The truth table: the response under each possible signer, and each embedded certificate under the CA.
        for signer in ["inter", "ocsp-responder", "ocsp-no-eku", "root"]:
            ok = verify(loaded[signer], resp.tbs_response_bytes, resp.signature, resp.signature_hash_algorithm)
            table[hashlib.sha256(spki(loaded[signer]) + resp.tbs_response_bytes + resp.signature).hexdigest()] = \
                "valid" if ok else "invalid"
        for embedded in resp.certificates:
            ok = verify(loaded["inter"], embedded.tbs_certificate_bytes, embedded.signature, embedded.signature_hash_algorithm)
            table[hashlib.sha256(spki(loaded["inter"]) + embedded.tbs_certificate_bytes + embedded.signature).hexdigest()] = \
                "valid" if ok else "invalid"
    # CertID hashes with hashlib, for issuers of three key types and sizes.
    certid_rows = []
    for issuer in ["root", "inter", "rsa-root"]:
        c = loaded[issuer]
        name_der = c.subject.public_bytes()
        bits = key_bits(c)
        certid_rows.append([issuer, hashlib.sha1(name_der).hexdigest(), hashlib.sha1(bits).hexdigest(),
                            hashlib.sha256(name_der).hexdigest(), hashlib.sha256(bits).hexdigest()])
    print(write_module(
        "crypto-pki", "ocsp-vectors.nra", "OcspVectors",
        ["python3 scripts/crypto-vectors/gen-pki-ocsp.py: `openssl ocsp` 3.5.7 records (gen-pki-material.py) read with",
         f"Python cryptography {cryptography.__version__}; CertID hashes from hashlib; signature truth table from cryptography."],
        [("certificates", ["name", "DER"], [[n, d.hex()] for n, d in certs.items()]),
         ("requests", ["name", "DER", "hash", "issuer name hash", "issuer key hash", "serial (hex)", "nonce",
                       "CertID count"], request_rows),
         ("responses", ["name", "DER", "responder name or key hash", "producedAt", "serial;status;this;next;revoked;reason|...",
                        "nonce", "certificates"], response_rows),
         ("cert_ids", ["issuer", "SHA-1 name hash", "SHA-1 key hash", "SHA-256 name hash", "SHA-256 key hash"], certid_rows),
         ("signatures", ["SHA-256(SPKI || signed || signature)", "valid or invalid"], [[k, v] for k, v in sorted(table.items())])]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
