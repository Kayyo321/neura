#!/usr/bin/env python3
"""Extract and independently validate Nitro attestation fixtures.

    python3 scripts/crypto-vectors/gen-enclave-nitro.py [sources-dir]

AWS's published sample is authentic debug evidence. The second independently
published report has a nonce and provisioned measurements. Both are checked
against the AWS-published DER fingerprint at their respective issue times.
Uses cbor2 5.6.5, cryptography 43.0.0 and OpenSSL 3.5.7.
"""

import datetime
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tempfile

import cbor2
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519, utils
from cryptography.x509.oid import NameOID

from nra_vectors import write_module

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/enclave/nitro")
ROOT_FINGERPRINT = "641a0321a3e244efe456463195d606317ed7cdcc3c1756e09893f3c68f79bb5b"
SOURCES = {
    "AWS_NitroEnclaves_Root-G1.pem": "6eb9688305e4bbca67f44b59c29a0661ae930f09b5945b5d1d9ae01125c8d6c0",
    "att_doc_sample.bin": "c9f773ce17c720028abc33639bb4ba3e61077164bbfa410510e4b545d7d1ebfe",
    "nitro-attestation-alpha-prerelease.cbor": "7b3cdc1ee62fe725f157a6f13ce486e12dc110f7cd9182d0b6efffc69b679269",
    "measurements-prerelease.json": "22edae8512f61915751eb9bf5f2ae80031827c1dec4362caf63614affc842d81",
    "credential-enclave-readme.md": "23322cad7397b61a9c0d1ead3de3d2cf2a53efefd76514cf52938fb4702b52d3",
}


def verified_report(raw, root_cert, scratch):
    protected, unprotected, payload, signature = cbor2.loads(raw)
    assert cbor2.loads(protected) == {1: -35} and unprotected == {} and len(signature) == 96
    claims = cbor2.loads(payload)
    leaf = x509.load_der_x509_certificate(claims["certificate"])
    bundle = [x509.load_der_x509_certificate(cert) for cert in claims["cabundle"]]
    assert bundle[0] == root_cert
    (scratch / "root.pem").write_bytes(root_cert.public_bytes(serialization.Encoding.PEM))
    (scratch / "leaf.pem").write_bytes(leaf.public_bytes(serialization.Encoding.PEM))
    (scratch / "intermediates.pem").write_bytes(
        b"".join(cert.public_bytes(serialization.Encoding.PEM) for cert in bundle[1:]))
    assert leaf.extensions.get_extension_for_class(x509.KeyUsage).value.digital_signature
    assert all(ca.extensions.get_extension_for_class(x509.KeyUsage).value.key_cert_sign for ca in bundle)
    subprocess.run(
        ["openssl", "verify", "-attime", str(claims["timestamp"] // 1000),
         "-no-CAfile", "-no-CApath", "-no-CAstore", "-trusted", "root.pem", "-untrusted",
         "intermediates.pem", "leaf.pem"],
        cwd=scratch, check=True, capture_output=True)
    sig_der = utils.encode_dss_signature(int.from_bytes(signature[:48], "big"), int.from_bytes(signature[48:], "big"))
    signed_bytes = cbor2.dumps(["Signature1", protected, b"", payload])
    leaf.public_key().verify(sig_der, signed_bytes, ec.ECDSA(hashes.SHA384()))
    return claims, [protected, unprotected, payload, signature]


def wrong_root():
    seed = hashlib.sha256(b"Supplementary offline Nitro negative test").digest()
    key = ed25519.Ed25519PrivateKey.from_private_bytes(seed)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Neura Nitro negative test CA")])
    utc = datetime.timezone.utc
    cert = (x509.CertificateBuilder().issuer_name(name).subject_name(name).public_key(key.public_key())
            .serial_number(1).not_valid_before(datetime.datetime(2020, 1, 1, tzinfo=utc))
            .not_valid_after(datetime.datetime(2030, 1, 1, tzinfo=utc))
            .add_extension(x509.BasicConstraints(ca=True, path_length=8), critical=True)
            .sign(key, None))
    return cert.public_bytes(serialization.Encoding.DER)


def report_row(raw, claims):
    timestamp = claims["timestamp"]
    return [raw.hex(), claims["module_id"], str(timestamp), str(timestamp // 1000), str(timestamp % 1000 * 1000000)]


def bad_documents(parts, positive):
    header, _, _, sig = parts
    duplicate = dict(positive)
    del duplicate["user_data"]
    # A definite-length map with the expected number of entries, including the nonce twice.
    keys = b"".join(cbor2.dumps(k) + cbor2.dumps(v) for k, v in duplicate.items())
    duplicate_map = bytes([0xA9]) + keys + cbor2.dumps("nonce") + cbor2.dumps(duplicate["nonce"])
    unsupported_digest = dict(positive, digest="SHA256")
    wrong_width = dict(positive, pcrs={0: bytes(47)})
    return [
        ["duplicate claim", cbor2.dumps([header, {}, duplicate_map, sig]).hex()],
        ["wrong PCR width", cbor2.dumps([header, {}, cbor2.dumps(wrong_width), sig]).hex()],
        ["wrong digest", cbor2.dumps([header, {}, cbor2.dumps(unsupported_digest), sig]).hex()],
        ["unprotected algorithm", cbor2.dumps([header, {1: -35}, cbor2.dumps(positive), sig]).hex()],
    ]


def main():
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    source = {name: (root / name).read_bytes() for name in SOURCES}
    for name, data in source.items():
        if hashlib.sha256(data).hexdigest() != SOURCES[name]:
            raise ValueError(f"unexpected SHA-256 for {name}")
    root_cert = x509.load_pem_x509_certificate(source["AWS_NitroEnclaves_Root-G1.pem"])
    assert root_cert.fingerprint(hashes.SHA256()).hex() == ROOT_FINGERPRINT
    aws_raw = source["att_doc_sample.bin"]
    positive_raw = source["nitro-attestation-alpha-prerelease.cbor"]
    with tempfile.TemporaryDirectory() as temporary:
        aws, _ = verified_report(aws_raw, root_cert, pathlib.Path(temporary))
        positive, positive_parts = verified_report(positive_raw, root_cert, pathlib.Path(temporary))
    assert aws["nonce"] is None and all(aws["pcrs"][index] == bytes(48) for index in range(3))
    expected_nonce = re.search(rb"--nonce ([0-9a-f]{64})", source["credential-enclave-readme.md"])[1].decode()
    assert positive["nonce"].hex() == expected_nonce
    measurements = json.loads(source["measurements-prerelease.json"])
    expected_pcrs = [measurements[f"pcr{index}"] for index in range(3)]
    assert expected_pcrs == [positive["pcrs"][index].hex() for index in range(3)]
    columns = ["encoded report", "module", "timestamp ms", "issued seconds", "issued nanos"]
    root_der = root_cert.public_bytes(serialization.Encoding.DER)
    print(write_module("crypto-enclave", "nitro-vectors.nra", "NitroVectors",
                       ["python3 scripts/crypto-vectors/gen-enclave-nitro.py",
                        "AWS Nitro known signed sample @14e13acb, provisioned example @cff16a34; see SOURCES.txt.",
                        "Verified with cbor2, cryptography and OpenSSL at the attestation issuance instants."],
                       [("roots", ["AWS root DER", "unrelated test CA DER"], [[root_der.hex(), wrong_root().hex()]]),
                        ("debug", columns, [report_row(aws_raw, aws)]),
                        ("production", columns + ["nonce", "PCR 0", "PCR 1", "PCR 2"],
                         [report_row(positive_raw, positive) + [expected_nonce, *expected_pcrs]]),
                        ("malformed", ["reason", "report"], bad_documents(positive_parts, positive))]))


if __name__ == "__main__":
    main()
