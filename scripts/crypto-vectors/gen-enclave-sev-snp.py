#!/usr/bin/env python3
"""Reproduce the Milan VCEK report and chain fixture from pinned go-sev-guest and AMD KDS."""

from datetime import datetime, timezone
import hashlib
from pathlib import Path
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from nra_vectors import write_module

SOURCES = Path("/home/dev/neura-gate/030/crypto2/sources/enclave")
DIGESTS = {
    "sev-attestation.bin": "377e6241d3b373ab1df80c0f96978594e7e21f4797dd6ea95e2957e1c1e26060",
    "sev-vcek.der": "0d057f9b6e29a69eda9c0154b259567d291c1c08d73a11e9d31ace07c435b6d8",
    "milan-cert-chain.pem": "22e62f8d2c21a156470145fc75f7b5a377cb053ced3e97f0bd3f8d8ca5941ce6",
}
SOURCE_COMMIT = "a30aab2ec76981a7ab4d6909df57f2ca9051d2b4"
AT = datetime(2024, 1, 1, tzinfo=timezone.utc)


def checked(sources, name):
    data = (sources / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != DIGESTS[name]:
        raise ValueError(f"unexpected SHA-256 for {name}")
    return data


def row(sources):
    report = checked(sources, "sev-attestation.bin")
    vcek_der = checked(sources, "sev-vcek.der")
    vcek = x509.load_der_x509_certificate(vcek_der)
    ask, ark = x509.load_pem_x509_certificates(checked(sources, "milan-cert-chain.pem"))
    vcek.verify_directly_issued_by(ask)
    ask.verify_directly_issued_by(ark)
    ark.verify_directly_issued_by(ark)
    if any(not (cert.not_valid_before_utc <= AT <= cert.not_valid_after_utc) for cert in (vcek, ask, ark)):
        raise ValueError("test instant must be within the certificate chain's validity")
    if len(report) != 1184 or int.from_bytes(report[:4], "little") != 2:
        raise ValueError("unexpected AMD report format")
    r = int.from_bytes(report[672:744], "little")
    s = int.from_bytes(report[744:816], "little")
    vcek.public_key().verify(utils.encode_dss_signature(r, s), report[:672], ec.ECDSA(hashes.SHA384()))
    oids = "1.3.6.1.4.1.3704.1."
    ext = lambda suffix: vcek.extensions.get_extension_for_oid(x509.ObjectIdentifier(oids + suffix)).value.value
    if ext("4") != report[416:480]:
        raise ValueError("unexpected VCEK chip ID")
    for suffix, offset in [("3.1", 384), ("3.2", 385), ("3.3", 390), ("3.8", 391)]:
        if ext(suffix) != bytes([2, 1, report[offset]]):
            raise ValueError("unexpected VCEK SPL")
    return [report.hex(), vcek_der.hex(), ask.public_bytes(serialization.Encoding.DER).hex(),
            ark.public_bytes(serialization.Encoding.DER).hex(), str(int(AT.timestamp())),
            report[80:144].hex(), report[144:192].hex(), str(int.from_bytes(report[384:392], "little"))]


def main():
    sources = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    print(write_module("crypto-enclave", "sev-snp-vectors.nra", "SevSnpVectors", [
        "python3 scripts/crypto-vectors/gen-enclave-sev-snp.py",
        f"Google go-sev-guest {SOURCE_COMMIT}, verify/testdata, and AMD Milan KDS v1 certificate chain.",
        "P-384 report and RSA-PSS certificate signatures independently validated by cryptography 43.",
    ], [("milan", ["report", "VCEK", "ASK", "ARK", "verification time", "report data", "measurement",
                    "reported TCB"], [row(sources)])]))


if __name__ == "__main__":
    main()
