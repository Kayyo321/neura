#!/usr/bin/env python3
"""Verify direct Intel PCS snapshots and generate deterministic Neura collateral inputs.

    python3 scripts/crypto-vectors/gen-enclave-dcap-pcs.py [quote-directory [pcs-directory]]

The quote directory holds the previously pinned production Phala captures and Intel
trust anchor. The PCS directory holds raw official HTTPS bodies, the selected
issuer and Date headers and the verified source manifest.
"""

import hashlib
import importlib
import json
import pathlib
import re
import subprocess
import sys
import tempfile
from datetime import datetime
from email.utils import parsedate_to_datetime
from urllib.parse import unquote_to_bytes

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.x509.oid import NameOID

from nra_vectors import write_module

reference = importlib.import_module("gen-enclave-dcap-collateral")
QUOTES = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/enclave/dcap")
PCS_DIRECTORY = "official-pcs-2026-11-18"
PCS_PIN_SHA256 = "b12bfb140b84e142201f3b048fbe94bbc978eada32de460a4d097a9271294a5b"
QE_CAPTURES = {
    "sgx-qe.json": "c5cef3a60db04a872cbaa3130573f6068cdda9923c69bd6a7bae321264c3f83f",
    "tdx-qe.json": "bfc60da49fbbfa603d9e5351bac5a9b980be2b47bb2480cb88b3e8b6786975a0",
    "sgx-qe-headers.json": "c7c99bf7f8a39624d2f734e885641b7bc9a777a7e86eca48d7faed39fe3e0eea",
    "tdx-qe-headers.json": "c7c99bf7f8a39624d2f734e885641b7bc9a777a7e86eca48d7faed39fe3e0eea",
}
VERIFY_AT = datetime.fromisoformat("2026-11-18T15:14:40+00:00")


def pinned(directory, filename, sha256):
    data = (directory / filename).read_bytes()
    assert hashlib.sha256(data).hexdigest() == sha256, filename
    return data


def validate_path(root, signer, scratch):
    (scratch / "root.pem").write_bytes(root.public_bytes(serialization.Encoding.PEM))
    (scratch / "signer.pem").write_bytes(signer.public_bytes(serialization.Encoding.PEM))
    subprocess.run(["openssl", "verify", "-x509_strict", "-attime", str(int(VERIFY_AT.timestamp())),
                    "-no-CAfile", "-no-CApath", "-no-CAstore", "-trusted", "root.pem", "signer.pem"],
                   cwd=scratch, check=True, capture_output=True)


def verify_signature(message, signature, public_key):
    assert len(signature) == 64
    der_value = utils.encode_dss_signature(int.from_bytes(signature[:32], "big"), int.from_bytes(signature[32:], "big"))
    public_key.verify(der_value, message, ec.ECDSA(hashes.SHA256()))


def read_sources(directory):
    captures = {}
    records = json.loads(pinned(directory, "tcb-crl-pins.json", PCS_PIN_SHA256))
    latest = max(parsedate_to_datetime(record["date"]) for record in records)
    assert latest == VERIFY_AT
    for record in records:
        name = record["name"]
        data = pinned(directory, name, record["sha256"])
        headers = json.loads(pinned(directory, name + ".headers.json", record["headers_sha256"]))
        assert headers["date"] == record["date"]
        captures[name] = data, headers
    for name in ("sgx-qe", "tdx-qe"):
        blob, headers = (pinned(directory, name + suffix, QE_CAPTURES[name + suffix])
                         for suffix in (".json", "-headers.json"))
        captures[name + ".json"] = blob, json.loads(headers)
    return captures


def issuer(headers, field, common_name, root, scratch):
    cert, embedded = x509.load_pem_x509_certificates(unquote_to_bytes(headers[field]))
    assert embedded == root
    assert [item.value for item in cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)] == [common_name]
    validate_path(root, cert, scratch)
    return cert


def document(capture, field, chain_header, root, scratch):
    raw, headers = capture
    signer = issuer(headers, chain_header, "Intel SGX TCB Signing", root, scratch)
    assert not signer.extensions.get_extension_for_class(x509.BasicConstraints).value.ca
    assert signer.extensions.get_extension_for_class(x509.KeyUsage).value.digital_signature
    text = raw.decode("ascii")
    prefix = re.match(r'^\s*{\s*"' + re.escape(field) + r'"\s*:\s*', text)
    claims, end = json.JSONDecoder().raw_decode(text, prefix.end())
    body = raw[prefix.end():end]
    outer = json.loads(text)
    assert set(outer) == {field, "signature"} and claims == outer[field]
    assert signer.public_key().curve.name == "secp256r1"
    signature = bytes.fromhex(outer["signature"])
    verify_signature(body, signature, signer.public_key())
    assert datetime.fromisoformat(claims["issueDate"]) <= VERIFY_AT < datetime.fromisoformat(claims["nextUpdate"])
    assert claims["tcbEvaluationDataNumber"] == 20
    return claims, signer, [body.hex(), signature.hex(), reference.der(signer).hex()]


def valid_crl(data, signer, subjects):
    crl = x509.load_der_x509_crl(data)
    assert signer.extensions.get_extension_for_class(x509.KeyUsage).value.crl_sign
    assert crl.issuer == signer.subject and crl.is_signature_valid(signer.public_key())
    assert crl.last_update_utc <= VERIFY_AT < crl.next_update_utc
    for cert in subjects:
        assert crl.get_revoked_certificate_by_serial_number(cert.serial_number) is None


def profile(tee, platform, quote, captures, root, scratch):
    exact = bytes.fromhex(reference.verify_quote(quote, root, scratch)[1])
    pck, quote_issuer, embedded = x509.load_pem_x509_certificates(exact[exact.index(b"-----BEGIN CERTIFICATE-----"):].rstrip(b"\x00"))
    assert embedded == root
    subprocess.run(["openssl", "verify", "-x509_strict", "-attime", str(int(VERIFY_AT.timestamp())),
                    "-no-CAfile", "-no-CApath", "-no-CAstore", "-trusted", "root.pem",
                    "-untrusted", "issuer.pem", "pck.pem"], cwd=scratch, check=True, capture_output=True)
    issuer_body, issuer_headers = captures[platform + "-crl.der"]
    pck_issuer = issuer(issuer_headers, "sgx-pck-crl-issuer-chain",
                        "Intel SGX PCK " + platform.title() + " CA", root, scratch)
    assert quote_issuer == pck_issuer
    tcb, tcb_signer, tcb_row = document(captures[tee + "-tcb.json"], "tcbInfo", "tcb-info-issuer-chain", root, scratch)
    qe, qe_signer, qe_row = document(captures[tee + "-qe.json"], "enclaveIdentity",
                                     "sgx-enclave-identity-issuer-chain", root, scratch)
    assert tcb["id"] == tee.upper() and tcb["version"] == 3 and tcb["tcbType"] == 0
    assert qe["version"] == 2
    root_crl, _ = captures["root-crl.der"]
    valid_crl(root_crl, root, [pck_issuer, tcb_signer, qe_signer])
    valid_crl(issuer_body, pck_issuer, [pck])
    td_report = exact[48:632] if tee == "tdx" else b""
    platform_status = reference.select_tcb(pck, tcb, td_report)
    qe_status = reference.select_qe(exact, qe)
    module_status = reference.select_module(td_report, tcb) if td_report else ""
    assert qe_status == "UpToDate"
    return [str(int(VERIFY_AT.timestamp())), reference.der(pck_issuer).hex(), root_crl.hex(), issuer_body.hex(),
            *tcb_row, *qe_row, platform_status, qe_status, module_status]


def main():
    quote_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else QUOTES
    pcs_path = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else quote_path / PCS_DIRECTORY
    captures = read_sources(pcs_path)
    root_pem = pinned(quote_path, "intel/vendor-root.pem", reference.SOURCE_HASHES["intel/vendor-root.pem"])
    root = x509.load_pem_x509_certificate(root_pem)
    assert root.fingerprint(hashes.SHA256()).hex() == reference.FINGERPRINT
    with tempfile.TemporaryDirectory() as directory:
        scratch = pathlib.Path(directory)
        sgx = profile("sgx", "processor", pinned(quote_path, "sgx-quote.bin", reference.SOURCE_HASHES["sgx-quote.bin"]),
                      captures, root, scratch)
        tdx = profile("tdx", "platform", pinned(quote_path, "phala-tdx-quote.bin", reference.SOURCE_HASHES["phala-tdx-quote.bin"]),
                      captures, root, scratch)
    assert sgx[-3:] == ["OutOfDateConfigurationNeeded", "UpToDate", ""]
    assert tdx[-3:] == ["OutOfDate", "UpToDate", "OutOfDate"]
    columns = ["verification epoch seconds", "PCK issuer DER", "root CRL", "PCK CRL", "TCB JSON",
               "TCB signature", "TCB signer DER", "QE JSON", "QE signature", "QE signer DER",
               "platform status", "QE status", "TDX module status"]
    print(write_module("crypto-enclave", "dcap-pcs-vectors.nra", "DcapPcsVectors",
                       ["python3 scripts/crypto-vectors/gen-enclave-dcap-pcs.py",
                        "Direct Intel PCS v4 snapshots, 2026-11-18T15:14:40Z, applied to genuine Phala quotes.",
                        "Exact Intel-signed JSON, paths and CRLs verified with OpenSSL 3.5.7 and cryptography 43.0.0."],
                       [("sgx", columns, [sgx]), ("tdx", columns, [tdx])]))


if __name__ == "__main__":
    main()
