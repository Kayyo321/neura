#!/usr/bin/env python3
"""Certificate Transparency (RFC 6962) vectors for Std.Crypto.Pki.Transparency.

Two sources:
- Real SCTs: the letsencrypt.org chain recorded with `openssl s_client` on 2026-11-17 and the keys of its two
  logs from Google's v3 log list (both under /home/dev/neura-gate/030/crypto2/sources/pki/ct, see SOURCES.txt).
  The SCT fields and the precertificate TBS come from Python `cryptography` (tbs_precertificate_bytes); the
  signed data is rebuilt here from RFC 6962 section 3.2; the truth table entry is cryptography's ECDSA check.
- A generated log: an RSA-2048 log key (zoo-rsa2048 of the recorded material; PKCS #1 v1.5 signatures are
  deterministic), SCTs for the material leaf (x509_entry, TLS list form), a certificate with an embedded SCT made
  here with cryptography (Ed25519, deterministic) from its precertificate, a 7-leaf log with every audit path and
  consistency proofs computed from RFC 6962 section 2.1, and its signed tree head in get-sth JSON form.

    python3 scripts/crypto-vectors/gen-pki-ct.py [sources-dir]
"""

import base64
import datetime
import hashlib
import pathlib
import struct
import sys

import cryptography
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, padding

from nra_vectors import write_module

SOURCES = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/pki")


def spki(key) -> bytes:
    return key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)


def u16(data: bytes) -> bytes:
    return struct.pack(">H", len(data)) + data


def u24(data: bytes) -> bytes:
    return struct.pack(">I", len(data))[1:] + data


def signed_entry(entry_type: int, cert_der: bytes, issuer_spki: bytes, tbs: bytes) -> bytes:
    if entry_type == 0:
        return struct.pack(">H", 0) + u24(cert_der)
    return struct.pack(">H", 1) + hashlib.sha256(issuer_spki).digest() + u24(tbs)


def sct_signed_data(timestamp: int, entry: bytes, extensions: bytes) -> bytes:
    return bytes([0, 0]) + struct.pack(">Q", timestamp) + entry + u16(extensions)


def serialize_sct(log_id: bytes, timestamp: int, extensions: bytes, hash_alg: int, sig_alg: int, sig: bytes) -> bytes:
    return bytes([0]) + log_id + struct.pack(">Q", timestamp) + u16(extensions) + bytes([hash_alg, sig_alg]) + u16(sig)


def mth(leaves):
    """RFC 6962 section 2.1 Merkle Tree Hash over leaf inputs."""
    if not leaves:
        return hashlib.sha256(b"").digest()
    if len(leaves) == 1:
        return hashlib.sha256(b"\x00" + leaves[0]).digest()
    k = 1
    while k * 2 < len(leaves):
        k *= 2
    return hashlib.sha256(b"\x01" + mth(leaves[:k]) + mth(leaves[k:])).digest()


def path(m, leaves):
    """RFC 6962 section 2.1.1 PATH(m, D[n])."""
    if len(leaves) <= 1:
        return []
    k = 1
    while k * 2 < len(leaves):
        k *= 2
    if m < k:
        return path(m, leaves[:k]) + [mth(leaves[k:])]
    return path(m - k, leaves[k:]) + [mth(leaves[:k])]


def subproof(m, leaves, b):
    """RFC 6962 section 2.1.2 SUBPROOF(m, D[n], b)."""
    n = len(leaves)
    if m == n:
        return [] if b else [mth(leaves)]
    k = 1
    while k * 2 < n:
        k *= 2
    if m <= k:
        return subproof(m, leaves[:k], b) + [mth(leaves[k:])]
    return subproof(m - k, leaves[k:], False) + [mth(leaves[:k])]


def main() -> int:
    sources = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    ct = sources / "ct"
    material = sources / "material"
    # Real SCTs.
    leaf_der = (ct / "chain-0.der").read_bytes()
    issuer_der = (ct / "chain-1.der").read_bytes()
    leaf = x509.load_der_x509_certificate(leaf_der)
    issuer = x509.load_der_x509_certificate(issuer_der)
    real_rows, table = [], {}
    logs = {}
    for path_ in sorted(ct.glob("log-*.spki.der")):
        key_der = path_.read_bytes()
        logs[hashlib.sha256(key_der).digest()] = key_der
    issuer_spki = spki(issuer.public_key())
    for sct in leaf.extensions.get_extension_for_class(x509.PrecertificateSignedCertificateTimestamps).value:
        timestamp = int(sct.timestamp.replace(tzinfo=datetime.timezone.utc).timestamp() * 1000)
        ext = sct.extension_bytes
        data = sct_signed_data(timestamp, signed_entry(1, leaf_der, issuer_spki, leaf.tbs_precertificate_bytes), ext)
        key_der = logs[sct.log_id]
        key = serialization.load_der_public_key(key_der)
        try:
            key.verify(sct.signature, data, ec.ECDSA(hashes.SHA256()))
            ok = "valid"
        except Exception:
            ok = "invalid"
        assert ok == "valid"
        table[hashlib.sha256(key_der + data + sct.signature).hexdigest()] = ok
        real_rows.append([sct.log_id.hex(), str(timestamp), "4", "3", sct.signature.hex(), ext.hex(), data.hex(), key_der.hex()])
    # The generated log.
    log_key = serialization.load_pem_private_key((material / "keys" / "zoo-rsa2048.pem").read_bytes(), None)
    log_spki = spki(log_key.public_key())
    log_id = hashlib.sha256(log_spki).digest()

    def sign(data: bytes) -> bytes:
        sig = log_key.sign(data, padding.PKCS1v15(), hashes.SHA256())
        table[hashlib.sha256(log_spki + data + sig).hexdigest()] = "valid"
        return sig

    material_leaf = (material / "certs" / "leaf.der").read_bytes()
    gen_scts, leaves, leaf_rows = [], [], []
    for timestamp, ext in [(1780000000000, b""), (1780000001234, b"\x01\x02\x03")]:
        entry = signed_entry(0, material_leaf, b"", b"")
        sig = sign(sct_signed_data(timestamp, entry, ext))
        gen_scts.append(serialize_sct(log_id, timestamp, ext, 4, 1, sig))
        leaves.append(bytes([0, 0]) + struct.pack(">Q", timestamp) + entry + u16(ext))
    tls_list = u16(b"".join(u16(s) for s in gen_scts))
    # A certificate with an embedded SCT, issued by policy-inter (Ed25519) from its precertificate.
    ca_cert = x509.load_der_x509_certificate((material / "certs" / "policy-inter.der").read_bytes())
    ca_key = serialization.load_pem_private_key((material / "keys" / "policy-inter.pem").read_bytes(), None)
    subject_key = serialization.load_pem_private_key((material / "keys" / "policy-leaf.pem").read_bytes(), None)

    def builder():
        return (x509.CertificateBuilder()
                .subject_name(x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME, "ct.example.com")]))
                .issuer_name(ca_cert.subject).public_key(subject_key.public_key()).serial_number(0x7e57)
                .not_valid_before(datetime.datetime(2025, 1, 1, tzinfo=datetime.timezone.utc))
                .not_valid_after(datetime.datetime(2030, 1, 1, tzinfo=datetime.timezone.utc))
                .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                .add_extension(x509.SubjectAlternativeName([x509.DNSName("ct.example.com")]), critical=False))

    precert = builder().sign(ca_key, None)
    ca_spki = spki(ca_cert.public_key())
    pre_timestamp = 1780000005000
    pre_entry = signed_entry(1, b"", ca_spki, precert.tbs_certificate_bytes)
    pre_sig = sign(sct_signed_data(pre_timestamp, pre_entry, b""))
    pre_sct = serialize_sct(log_id, pre_timestamp, b"", 4, 1, pre_sig)
    inner = u16(u16(pre_sct))
    octet = bytes([0x04]) + (bytes([len(inner)]) if len(inner) < 128 else bytes([0x81, len(inner)]) if len(inner) < 256
                             else bytes([0x82]) + struct.pack(">H", len(inner))) + inner
    final = builder().add_extension(x509.UnrecognizedExtension(x509.ObjectIdentifier("1.3.6.1.4.1.11129.2.4.2"), octet),
                                    critical=False).sign(ca_key, None)
    assert final.tbs_precertificate_bytes == precert.tbs_certificate_bytes
    final_der = final.public_bytes(serialization.Encoding.DER)
    leaves.append(bytes([0, 0]) + struct.pack(">Q", pre_timestamp) + pre_entry + u16(b""))
    # Fillers: x509 entries of other recorded certificates.
    for i, name in enumerate(["root", "inter", "neg-wildcard", "rsa-root"]):
        leaves.append(bytes([0, 0]) + struct.pack(">Q", 1780000010000 + i) + signed_entry(0, (material / "certs" / f"{name}.der")
                                                                                          .read_bytes(), b"", b"") + u16(b""))
    root = mth(leaves)
    proof_rows = []
    for size in [1, 2, 3, 5, 7]:
        for m in range(size):
            proof_rows.append(["inclusion", str(m), str(size), hashlib.sha256(b"\x00" + leaves[m]).hexdigest(),
                               ",".join(h.hex() for h in path(m, leaves[:size])), mth(leaves[:size]).hex()])
    for old, new in [(1, 7), (2, 7), (3, 7), (4, 7), (6, 7), (3, 5), (7, 7)]:
        proof_rows.append(["consistency", str(old), str(new), mth(leaves[:old]).hex(),
                           ",".join(h.hex() for h in subproof(old, leaves[:new], True)), mth(leaves[:new]).hex()])
    sth_timestamp = 1780000020000
    tree_head = bytes([0, 1]) + struct.pack(">QQ", sth_timestamp, len(leaves)) + root
    sth_sig = sign(tree_head)
    sth_json = ("<'tree_size':" + str(len(leaves)) + ",'timestamp':" + str(sth_timestamp) + ",'sha256_root_hash':'"
                + base64.b64encode(root).decode() + "','tree_head_signature':'"
                + base64.b64encode(bytes([4, 1]) + u16(sth_sig)).decode() + "'>")
    print(write_module(
        "crypto-pki", "ct-vectors.nra", "CtVectors",
        ["python3 scripts/crypto-vectors/gen-pki-ct.py: letsencrypt.org SCTs (recorded 2026-11-17) read with Python",
         f"cryptography {cryptography.__version__}; a generated RSA log (RFC 6962 structures built here, Merkle proofs per",
         "section 2.1); signature truth table from cryptography."],
        [("real", ["certificate", "issuer"], [[leaf_der.hex(), issuer_der.hex()]]),
         ("real_scts", ["log ID", "timestamp ms", "hash", "signature algorithm", "signature", "extensions",
                        "signed data", "log SPKI"], real_rows),
         ("generated", ["log SPKI", "TLS SCT list for the material leaf", "material leaf", "CA (policy-inter)",
                        "certificate with an embedded SCT", "precertificate TBS"],
          [[log_spki.hex(), tls_list.hex(), material_leaf.hex(), ca_cert.public_bytes(serialization.Encoding.DER).hex(),
            final_der.hex(), precert.tbs_certificate_bytes.hex()]]),
         ("leaves", ["MerkleTreeLeaf"], [[leaf.hex()] for leaf in leaves]),
         ("proofs", ["kind", "index or old size", "tree size or new size", "leaf hash or old root", "proof", "root"],
          proof_rows),
         ("sth", ["get-sth JSON with ' for each double quote and < > for the braces", "tree size", "root"], [[sth_json, str(len(leaves)), root.hex()]]),
         ("signatures", ["SHA-256(SPKI || signed || signature)", "valid or invalid"], [[k, v] for k, v in sorted(table.items())])]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
