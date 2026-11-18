#!/usr/bin/env python3
"""Generate SGX v3 and TDX v4 DCAP signature-and-format fixtures.

    python3 scripts/crypto-vectors/gen-enclave-dcap.py [sources-dir]

Authentic quotes come from Phala's dcap-qvl and Google's go-tdx-guest, pinned
in SOURCES.txt. Their embedded PCK paths are checked against the Intel PCS
production root with OpenSSL 3.5.7. QE binding and both P-256 signatures
are checked independently with hashlib and cryptography 43.0.0.
"""

import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from nra_vectors import write_module

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/enclave/dcap")
ROOT_FINGERPRINT = "44a0196b2b99f889b8e149e95b807a350e7424964399e885a7cbb8ccfab674d3"
SOURCES = {
    "sgx-quote.bin": "f8b81014b6e443609746822194910f5dc1c92c322fa0584298d1e33e505ca3b5",
    "tdx-quote.bin": "6dde5548bec99147fef832643301f113df99931547be26df8ac376c4eaa5b5a7",
    "intel/vendor-root.pem": "267a851c8d10982685b5f219d9ac2600ba71463569a6541827c2dc9fe9d6d699",
    "intel/sgx/trustedRootCaCert.pem": "3e2c60b6c51c0f5eb4726040f7c42c4d2d54916d9c96a2562f7e87070b76450c",
}


def der_signature(raw):
    return utils.encode_dss_signature(int.from_bytes(raw[:32], "big"), int.from_bytes(raw[32:], "big"))


def report(raw, root, scratch):
    version, key_type, tee_type = struct.unpack_from("<HHI", raw)
    assert key_type == 2 and (version, tee_type) in ((3, 0), (4, 129))
    platform = "sgx" if version == 3 else "tdx"
    signed_end = 432 if version == 3 else 632
    sig_start = signed_end + 4
    auth_bytes, = struct.unpack_from("<I", raw, signed_end)
    full_length = sig_start + auth_bytes
    suffix = raw[full_length:]
    quote = raw[:full_length]
    signature = quote[sig_start:sig_start + 64]
    attestation_key = quote[sig_start + 64:sig_start + 128]
    qe_start = sig_start + 128
    if version == 4:
        cert_type, cert_len = struct.unpack_from("<HI", quote, qe_start)
        assert cert_type == 6
        qe_start += 6
        assert qe_start + cert_len == len(quote)
    qe_report = quote[qe_start:qe_start + 384]
    qe_sig_offset = qe_start + 384
    qe_signature = quote[qe_sig_offset:qe_sig_offset + 64]
    auth_start = qe_sig_offset + 64 + 2
    auth_length, = struct.unpack_from("<H", quote, auth_start - 2)
    cert_start = auth_start + auth_length
    qe_authentication = quote[auth_start:cert_start]
    cert_type, cert_len = struct.unpack_from("<HI", quote, cert_start)
    assert cert_type == 5 and cert_start + 6 + cert_len == len(quote)
    pck, issuer, embedded_root = x509.load_pem_x509_certificates(quote[cert_start + 6:].rstrip(b"\x00"))
    assert embedded_root == root
    pck.public_key().verify(der_signature(qe_signature), qe_report, ec.ECDSA(hashes.SHA256()))
    assert qe_report[320:] == hashlib.sha256(attestation_key + qe_authentication).digest() + bytes(32)
    quote_key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), b"\x04" + attestation_key)
    quote_key.verify(der_signature(signature), quote[:signed_end], ec.ECDSA(hashes.SHA256()))
    at = int(pck.not_valid_before_utc.timestamp()) + 86400
    for name, cert in (("root", root), ("issuer", issuer), ("pck", pck)):
        (scratch / f"{name}.pem").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    subprocess.run(
        ["openssl", "verify", "-x509_strict", "-attime", str(at), "-no-CAfile", "-no-CApath", "-no-CAstore",
         "-trusted", "root.pem", "-untrusted", "issuer.pem", "pck.pem"],
        cwd=scratch, check=True, capture_output=True)
    report_data = quote[signed_end - 64:signed_end]
    measurement = quote[112:144] if version == 3 else quote[184:232]
    return [platform, quote.hex(), str(at), report_data.hex(), measurement.hex(), str(qe_sig_offset), str(auth_start),
            suffix.hex()]


def main():
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    source = {name: (root / name).read_bytes() for name in SOURCES}
    for name, data in source.items():
        if hashlib.sha256(data).hexdigest() != SOURCES[name]:
            raise SystemExit(f"unexpected SHA-256 for {name}")
    trusted = x509.load_pem_x509_certificate(source["intel/vendor-root.pem"])
    assert trusted.fingerprint(hashes.SHA256()).hex() == ROOT_FINGERPRINT
    other = x509.load_pem_x509_certificate(source["intel/sgx/trustedRootCaCert.pem"])
    assert trusted != other
    with tempfile.TemporaryDirectory() as directory:
        rows = [report(source[name], trusted, pathlib.Path(directory)) for name in ("sgx-quote.bin", "tdx-quote.bin")]
    assert rows[0][-1] == ""
    assert bytes.fromhex(rows[1][-1]) == b"\nextra bytes(only for testing purpose)\n"
    anchor_rows = [[cert.public_bytes(serialization.Encoding.DER).hex() for cert in (trusted, other)]]
    print(write_module("crypto-enclave", "dcap-vectors.nra", "DcapVectors",
                       ["python3 scripts/crypto-vectors/gen-enclave-dcap.py",
                        "SGX v3 dcap-qvl@689f0084 and TDX v4 go-tdx-guest@fdf839f6. Intel PCS production root.",
                        "The source TDX fixture appends a suffix after its declared quote; it is marked separately.",
                        "OpenSSL 3.5.7, cryptography 43.0.0 and hashlib verify cryptographic expectations."],
                       [("roots", ["production root DER", "unrelated Intel QVL sample root DER"], anchor_rows),
                        ("quotes", ["TEE", "quote", "time seconds", "REPORTDATA", "measurement", "QE signature offset",
                                    "QE authentication offset", "capture suffix"], rows)]))


if __name__ == "__main__":
    main()
