#!/usr/bin/env python3
"""Extract and independently validate the public Phala SGX v3 and TDX v4 collateral.

    python3 scripts/crypto-vectors/gen-enclave-dcap-collateral.py [source-dir]

See enclave/dcap/SOURCES.txt for pinned quotes, Intel PCS root and collaterals.
The exact Intel-signed JSON is extracted without reserializing it. Python
cryptography 43 and OpenSSL 3.5.7 independently check quote and document
signatures, certificate paths, full CRLs, and the selected platform, QE and
TDX module TCB levels at 2025-06-20T00:00:00Z.
"""

import datetime
import hashlib
import importlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from nra_vectors import write_module

verify_quote = importlib.import_module("gen-enclave-dcap").report

SOURCES = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/enclave/dcap")
FINGERPRINT = "44a0196b2b99f889b8e149e95b807a350e7424964399e885a7cbb8ccfab674d3"
SOURCE_HASHES = {
    "sgx-quote.bin": "f8b81014b6e443609746822194910f5dc1c92c322fa0584298d1e33e505ca3b5",
    "sgx-quote-collateral.json": "bdd694bbe50f3a2a1cfe12f9e2bd83125921107a368edcf10780a5523b8501ce",
    "phala-tdx-quote.bin": "c42f9164325024bca2757bc8819b11879a0a369132ea4e2b7c85df4805ea72db",
    "tdx-quote-collateral.json": "b0a5f5fd620a8881b1eda45261fdf30dd930b49aff93231556645c81fcb4c0bc",
    "intel/vendor-root.pem": "267a851c8d10982685b5f219d9ac2600ba71463569a6541827c2dc9fe9d6d699",
}
PCK_EXTENSION = "1.2.840.113741.1.13.1"
AT = datetime.datetime(2025, 6, 20, tzinfo=datetime.timezone.utc)


def der(cert):
    return cert.public_bytes(serialization.Encoding.DER)


def tlvs(contents):
    cursor = 0
    while cursor < len(contents):
        tag, length = contents[cursor:cursor + 2]
        cursor += 2
        if length & 0x80:
            length_bytes = length & 0x7F
            assert 0 < length_bytes <= 2
            length = int.from_bytes(contents[cursor:cursor + length_bytes], "big")
            cursor += length_bytes
        stop = cursor + length
        assert stop <= len(contents)
        yield tag, contents[cursor:stop]
        cursor = stop


def dotted(encoded):
    first, *rest = encoded
    parts = [first // 40, first % 40]
    number = 0
    for digit in rest:
        number = (number << 7) | (digit & 127)
        if digit < 128:
            parts.append(number)
            number = 0
    assert number == 0 and encoded[-1] < 128
    return ".".join(str(p) for p in parts)


def pck_entries(contents):
    results = {}
    for tag, item in tlvs(contents):
        assert tag == 0x30
        (id_tag, oid), value = list(tlvs(item))
        assert id_tag == 6 and dotted(oid) not in results
        results[dotted(oid)] = value
    return results


def select_tcb(pck, tcb, td_report):
    tag, components = list(tlvs(pck.extensions.get_extension_for_oid(x509.ObjectIdentifier(PCK_EXTENSION)).value.value))[0]
    assert tag == 0x30
    extensions = pck_entries(components)
    assert bytes.fromhex(tcb["fmspc"]) == extensions[PCK_EXTENSION + ".4"][1]
    assert bytes.fromhex(tcb["pceId"]) == extensions[PCK_EXTENSION + ".3"][1]
    signed_tcb = pck_entries(extensions[PCK_EXTENSION + ".2"][1])
    actual = [int.from_bytes(signed_tcb[PCK_EXTENSION + f".2.{n}"][1], "big") for n in range(1, 18)]
    assert bytes(actual[:16]) == signed_tcb[PCK_EXTENSION + ".2.18"][1]
    assert extensions[PCK_EXTENSION + ".5"][1] == (b"\x01" if td_report else b"\x00")
    keys = []
    for level in tcb["tcbLevels"]:
        fields = level["tcb"]
        key = [entry["svn"] for entry in fields["sgxtcbcomponents"]] + [fields["pcesvn"]]
        if td_report:
            key += [entry["svn"] for entry in fields["tdxtcbcomponents"]]
        keys.append(tuple(key))
    assert keys == sorted(set(keys), reverse=True)
    for level in tcb["tcbLevels"]:
        requirements = [entry["svn"] for entry in level["tcb"]["sgxtcbcomponents"]]
        requirements.append(level["tcb"]["pcesvn"])
        if all(got >= need for got, need in zip(actual, requirements, strict=True)):
            if td_report:
                assert tcb["id"] == "TDX"
                svn = td_report[:16]
                start = 2 if svn[1] else 0
                required = [entry["svn"] for entry in level["tcb"]["tdxtcbcomponents"]]
                if any(got < need for got, need in zip(svn[start:], required[start:], strict=True)):
                    continue
            return level["tcbStatus"]
    raise ValueError("no reference TCB level matches")


def select_qe(quote, identity):
    version = int.from_bytes(quote[:2], "little")
    offset = 432 + 4 + 2 * 64 if version == 3 else 632 + 4 + 2 * 64 + 6
    assert identity["id"] == ("QE" if version == 3 else "TD_QE")
    report = quote[offset:offset + 384]
    misc = int.from_bytes(report[16:20], "little")
    misc_mask = int.from_bytes(bytes.fromhex(identity["miscselectMask"]), "little")
    misc_expected = int.from_bytes(bytes.fromhex(identity["miscselect"]), "little")
    assert misc & misc_mask == misc_expected
    assert bytes(a & b for a, b in zip(report[48:64], bytes.fromhex(identity["attributesMask"]), strict=True)) ==         bytes.fromhex(identity["attributes"])
    assert report[128:160] == bytes.fromhex(identity["mrsigner"])
    product, svn = struct.unpack_from("<HH", report, 256)
    assert product == identity["isvprodid"]
    return next(level["tcbStatus"] for level in identity["tcbLevels"] if svn >= level["tcb"]["isvsvn"])


def select_module(td_report, tcb):
    minor, major = td_report[:2]
    assert major == 1
    identity, = [entry for entry in tcb["tdxModuleIdentities"] if entry["id"] == f"TDX_{major:02X}"]
    assert td_report[64:112] == bytes.fromhex(identity["mrsigner"])
    seam_attributes = td_report[112:120]
    assert seam_attributes == bytes(8)
    assert bytes(a & b for a, b in zip(seam_attributes, bytes.fromhex(identity["attributesMask"]), strict=True)) == (
        bytes.fromhex(identity["attributes"]))
    return next(level["tcbStatus"] for level in identity["tcbLevels"] if minor >= level["tcb"]["isvsvn"])


def verified_body(collateral, name, root, scratch):
    cert, embedded = x509.load_pem_x509_certificates(collateral[name + "_issuer_chain"].encode())
    assert embedded == root
    (scratch / "root.pem").write_bytes(root.public_bytes(serialization.Encoding.PEM))
    (scratch / "signer.pem").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    subprocess.run(
        ["openssl", "verify", "-attime", str(int(AT.timestamp())), "-no-CAfile", "-no-CApath", "-no-CAstore",
         "-trusted", "root.pem", "signer.pem"],
        cwd=scratch, check=True, capture_output=True)
    body = collateral[name].encode("utf-8")
    raw_sig = bytes.fromhex(collateral[name + "_signature"])
    sig = utils.encode_dss_signature(int.from_bytes(raw_sig[:32], "big"), int.from_bytes(raw_sig[32:], "big"))
    cert.public_key().verify(sig, body, ec.ECDSA(hashes.SHA256()))
    claims = json.loads(body)
    assert datetime.datetime.fromisoformat(claims["issueDate"]) <= AT < datetime.datetime.fromisoformat(claims["nextUpdate"])
    return claims, cert, [body.hex(), raw_sig.hex(), der(cert).hex()]


def check_crl(blob, issuer, targets):
    crl = x509.load_der_x509_crl(blob)
    assert crl.is_signature_valid(issuer.public_key()) and crl.issuer == issuer.subject
    assert crl.last_update_utc <= AT < crl.next_update_utc
    for target in targets:
        assert crl.get_revoked_certificate_by_serial_number(target.serial_number) is None


def verify_cases(quote, collateral, root, td, scratch):
    evidence = verify_quote(quote, root, scratch)
    if td:
        assert bytes.fromhex(evidence[-1]) == bytes(70)
    else:
        assert evidence[-1] == ""
    exact_quote = bytes.fromhex(evidence[1])
    first = exact_quote.index(b"-----BEGIN CERTIFICATE-----")
    pck, issuer, embedded = x509.load_pem_x509_certificates(exact_quote[first:].rstrip(b"\x00"))
    assert embedded == root and pck.not_valid_before_utc <= AT < pck.not_valid_after_utc
    assert x509.load_pem_x509_certificates(collateral["pck_crl_issuer_chain"].encode()) == [issuer, root]
    subprocess.run(
        ["openssl", "verify", "-x509_strict", "-attime", str(int(AT.timestamp())), "-no-CAfile", "-no-CApath", "-no-CAstore",
         "-trusted", "root.pem", "-untrusted", "issuer.pem", "pck.pem"],
        cwd=scratch, check=True, capture_output=True)
    tcb, tcb_signer, tcb_row = verified_body(collateral, "tcb_info", root, scratch)
    qe, qe_signer, qe_row = verified_body(collateral, "qe_identity", root, scratch)
    root_crl, pck_crl = (bytes.fromhex(collateral[n]) for n in ("root_ca_crl", "pck_crl"))
    check_crl(root_crl, root, [issuer, tcb_signer, qe_signer])
    check_crl(pck_crl, issuer, [pck])
    report = exact_quote[48:632] if td else b""
    tcb_status, qe_status = select_tcb(pck, tcb, report), select_qe(exact_quote, qe)
    assert qe_status == "UpToDate"
    row = [str(int(AT.timestamp())), der(issuer).hex(), root_crl.hex(), pck_crl.hex(),
           *tcb_row, *qe_row, tcb_status, qe_status]
    if td:
        module_status = select_module(report, tcb)
        assert tcb_status == module_status == "UpToDate"
        assert not report[120] & 1
        row += [module_status, *[evidence[n] for n in (1, 3, 4, 7)]]
    else:
        assert tcb_status == "ConfigurationAndSWHardeningNeeded"
    return row


def main():
    root_dir = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    inputs = {name: (root_dir / name).read_bytes() for name in SOURCE_HASHES}
    for name, data in inputs.items():
        if hashlib.sha256(data).hexdigest() != SOURCE_HASHES[name]:
            raise SystemExit(f"incorrect pinned SHA-256 for {name}")
    root = x509.load_pem_x509_certificate(inputs["intel/vendor-root.pem"])
    assert root.fingerprint(hashes.SHA256()).hex() == FINGERPRINT
    with tempfile.TemporaryDirectory() as directory:
        scratch = pathlib.Path(directory)
        sgx_row = verify_cases(inputs["sgx-quote.bin"], json.loads(inputs["sgx-quote-collateral.json"]), root, False, scratch)
        tdx_row = verify_cases(inputs["phala-tdx-quote.bin"], json.loads(inputs["tdx-quote-collateral.json"]), root, True, scratch)
    columns = ["verification time", "PCK issuer DER", "root CRL", "PCK CRL", "TCB JSON",
               "TCB signature", "TCB signer DER", "QE JSON", "QE signature", "QE signer DER",
               "selected TCB status", "selected QE status"]
    print(write_module("crypto-enclave", "dcap-collateral-vectors.nra", "DcapCollateralVectors",
                       ["python3 scripts/crypto-vectors/gen-enclave-dcap-collateral.py",
                        "Phala dcap-qvl @689f0084 SGX v3 and TDX v4, authenticated to the Intel PCS production root.",
                        "Checked with OpenSSL 3.5.7, Python cryptography 43, and independent TCB comparisons."],
                       [("sgx", columns, [sgx_row]),
                        ("tdx", columns + ["selected module status", "quote", "REPORTDATA", "measurement",
                                           "capture suffix"], [tdx_row])]))


if __name__ == "__main__":
    main()
