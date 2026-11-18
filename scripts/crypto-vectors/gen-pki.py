#!/usr/bin/env python3
"""Known answers for Std.Crypto.Pki: DER, PEM, OIDs and the typed X.509 ASTs.

Sources (all recorded; this script only transcodes and computes):
- `openssl asn1parse -genstr` (OpenSSL 3.5.7) encodes the OBJECT IDENTIFIER, INTEGER, string and time
  samples; the expected values are the inputs given to it, and times are converted with Python's
  calendar.timegm.
- The certificates, CSRs, keys and encrypted PKCS#8 files that gen-pki-material.py recorded under
  /home/dev/neura-gate/030/crypto2/sources/pki/material (first argument overrides). Their expected
  fields are read with Python `cryptography` (version recorded in the output), an independent parser.
- PBES2 with AES-GCM (RFC 8018 + RFC 5084), which `openssl pkcs8` refuses ("AEAD ciphers not
  supported"), is made here with `cryptography`'s AESGCM and hashlib.pbkdf2_hmac over fixed salt and
  nonce, and DER written by hand from the RFC's ASN.1.
- The malformed DER inputs are built here, one per DER rule (X.690 sections 8 and 10); the expected
  error offset is the byte that breaks the rule.

    python3 scripts/crypto-vectors/gen-pki.py [material-dir]
"""

import calendar
import datetime
import hashlib
import ipaddress
import pathlib
import subprocess
import sys
import tempfile

import cryptography
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

from nra_vectors import write_module

MATERIAL = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/pki/material")


def genstr(spec: str) -> bytes:
    with tempfile.TemporaryDirectory() as d:
        out = pathlib.Path(d) / "x.der"
        subprocess.run(["openssl", "asn1parse", "-genstr", spec, "-out", str(out), "-noout"], check=True,
                       stdout=subprocess.DEVNULL)
        return out.read_bytes()


def tlv(tag: int, content: bytes) -> bytes:
    n = len(content)
    if n < 128:
        return bytes([tag, n]) + content
    body = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([tag, 0x80 | len(body)]) + body + content


def der_tables():
    oid_rows = []
    for dotted in ["1.2.840.113549.1.1.11", "2.5.4.3", "0.9.2342.19200300.100.1.25", "1.3.6.1.4.1.311.60.2.1.3",
                   "2.999.3", "2.25.329800735698586629295641978511506172918", "1.0", "0.39", "2.40.1",
                   "2.16.840.1.101.3.4.3.17", "1.3.6.1.4.1.11129.2.4.2"]:
        der = genstr("OID:" + dotted)
        oid_rows.append([dotted, der.hex()])
    int_rows = []
    for value in [0, 1, 127, 128, 255, 256, -1, -128, -129, -256, 2**63 - 1, -2**63, 2**64 + 5, 2**159]:
        der = genstr(f"INTEGER:{value}")
        int_rows.append([str(value), der.hex()])
    time_rows = []
    for spec in ["UTCTIME:500101000000Z", "UTCTIME:491231235959Z", "UTCTIME:240229120000Z", "UTCTIME:991231235959Z",
                 "GENTIME:20500101000000Z", "GENTIME:19500101000000Z", "GENTIME:99991231235959Z",
                 "GENTIME:20000229000000Z"]:
        der = genstr(spec)
        text = spec.split(":", 1)[1]
        if spec.startswith("UTC"):
            yy = int(text[:2])
            year = 1900 + yy if yy >= 50 else 2000 + yy
            rest = text[2:]
        else:
            year = int(text[:4])
            rest = text[4:]
        dt = datetime.datetime(year, int(rest[0:2]), int(rest[2:4]), int(rest[4:6]), int(rest[6:8]), int(rest[8:10]))
        time_rows.append([der.hex(), str(calendar.timegm(dt.timetuple()))])
    string_rows = []
    for spec, text in [("FORMAT:UTF8,UTF8String:héllo 世界", "héllo 世界"), ("PRINTABLESTRING:Neura Test (x)=?", "Neura Test (x)=?"),
                       ("IA5STRING:a@b.example", "a@b.example"), ("VISIBLESTRING:Visible ~", "Visible ~"),
                       ("FORMAT:UTF8,BMPSTRING:hé 世", "hé 世"), ("FORMAT:UTF8,UNIVERSALSTRING:a\U0001F600", "a\U0001F600"),
                       ("T61STRING:abc", "abc"), ("NUMERICSTRING:12 34", "12 34")]:
        der = genstr(spec)
        string_rows.append([der.hex(), text.encode("utf-8").hex()])
    # Malformed inputs: hex, expected error offset, description.
    bad = []

    def add(data: bytes, offset: int, why: str):
        bad.append([data.hex(), str(offset), why])

    add(b"", 0, "empty input")
    add(bytes([0x30]), 1, "missing length")
    add(bytes([0x30, 0x03, 0x02, 0x01]), 2, "content runs past the end")
    add(bytes([0x30, 0x80, 0x00, 0x00]), 1, "indefinite length")
    add(bytes([0x04, 0x81, 0x05]) + b"abcde", 2, "long form for a short length")
    add(bytes([0x04, 0x82, 0x00, 0x80]) + b"x" * 128, 2, "leading zero length byte")
    add(bytes([0x04, 0x85, 1, 0, 0, 0, 0]), 1, "length of five bytes")
    add(bytes([0x04, 0x84, 0x80, 0, 0, 0]), 2, "length at or above 2^31")
    add(bytes([0x04, 0x84, 0x00, 0x10, 0x00, 0x00]), 2, "four-byte length with a leading zero")
    add(bytes([0x04, 0x83, 0x01, 0x00, 0x00]) + b"x" * 10, 5, "truncated long content")
    add(bytes([0x1f, 0x80, 0x01, 0x00]), 1, "high tag number with a leading 0x80")
    add(bytes([0x1f, 0x1e, 0x00]), 0, "high tag form for a low tag number")
    add(bytes([0x1f, 0x81]), 2, "truncated high tag number")
    add(bytes([0x1f, 0x81, 0x81, 0x81, 0x01, 0x00]), 4, "tag number too large")
    add(bytes([0x00, 0x00]), 0, "end-of-contents tag")
    add(bytes([0x10, 0x00]), 0, "primitive SEQUENCE")
    add(bytes([0x24, 0x03, 0x04, 0x01, 0x41]), 0, "constructed OCTET STRING")
    add(bytes([0x30, 0x03, 0x02, 0x01, 0x05, 0x00]), 5, "trailing data")
    add(bytes([0x31, 0x06, 0x02, 0x01, 0x05, 0x02, 0x01, 0x03]), 5, "SET OF out of order")
    add(bytes([0x30, 0x04, 0x30, 0x02, 0x30, 0x80]), 5, "indefinite length nested")
    deep = bytes([0x05, 0x00])
    for _ in range(40):
        deep = tlv(0x30, deep)
    add(deep, 66, "nesting deeper than 32")
    bad_value = []

    def add_value(kind: str, data: bytes, offset: int, why: str):
        bad_value.append([kind, data.hex(), str(offset), why])

    add_value("boolean", bytes([0x01, 0x01, 0x01]), 2, "BOOLEAN 0x01")
    add_value("boolean", bytes([0x01, 0x02, 0xff, 0xff]), 0, "BOOLEAN of two bytes")
    add_value("integer", bytes([0x02, 0x02, 0x00, 0x7f]), 2, "INTEGER with a redundant 0x00")
    add_value("integer", bytes([0x02, 0x02, 0xff, 0x80]), 2, "INTEGER with a redundant 0xFF")
    add_value("integer", bytes([0x02, 0x00]), 0, "empty INTEGER")
    add_value("bitstring", bytes([0x03, 0x02, 0x01, 0x01]), 3, "BIT STRING unused bit set")
    add_value("bitstring", bytes([0x03, 0x02, 0x08, 0x00]), 2, "BIT STRING unused count 8")
    add_value("bitstring", bytes([0x03, 0x01, 0x01]), 2, "empty BIT STRING with unused bits")
    add_value("bitstring", bytes([0x03, 0x00]), 0, "BIT STRING without the count byte")
    add_value("oid", bytes([0x06, 0x00]), 2, "empty OID")
    add_value("oid", bytes([0x06, 0x02, 0x2a, 0x86]), 3, "truncated OID subidentifier")
    add_value("oid", bytes([0x06, 0x03, 0x2a, 0x80, 0x01]), 3, "OID subidentifier with 0x80")
    add_value("null", bytes([0x05, 0x01, 0x00]), 0, "NULL with content")
    add_value("text", bytes([0x13, 0x02, 0x61, 0x2a]), 3, "PrintableString with '*'")
    add_value("text", bytes([0x16, 0x01, 0x80]), 2, "IA5String with 0x80")
    add_value("text", bytes([0x0c, 0x02, 0xc3, 0x28]), 2, "invalid UTF-8")
    add_value("text", bytes([0x1e, 0x03, 0x00, 0x61, 0x00]), 2, "BMPString of odd length")
    add_value("text", bytes([0x1e, 0x02, 0xd8, 0x00]), 2, "BMPString surrogate")
    add_value("time", tlv(0x17, b"491231235960Z"), 12, "UTCTime second 60")
    add_value("time", tlv(0x17, b"490230000000Z"), 6, "UTCTime February 30")
    add_value("time", tlv(0x17, b"491301000000Z"), 4, "UTCTime month 13")
    add_value("time", tlv(0x17, b"4912312359Z"), 2, "UTCTime without seconds")
    add_value("time", tlv(0x17, b"491231235959+0100"), 2, "UTCTime with an offset")
    add_value("time", tlv(0x18, b"20500101000000.5Z"), 2, "GeneralizedTime with a fraction")
    add_value("time", tlv(0x18, b"2050010100000aZ"), 15, "GeneralizedTime with a letter")
    add_value("time", tlv(0x18, b"20500101000000z"), 16, "GeneralizedTime with a lowercase z")
    add_value("time", tlv(0x18, b"19000229000000Z"), 8, "GeneralizedTime 1900-02-29")
    return oid_rows, int_rows, time_rows, string_rows, bad, bad_value


def der_items(b: bytes, start: int = 0, end: int = None):
    """A small DER splitter used only to cut out spans (SPKI, key bytes) the expected values need."""
    end = len(b) if end is None else end
    out = []
    i = start
    while i < end:
        tag = b[i]
        n = b[i + 1]
        j = i + 2
        if n & 0x80:
            k = n & 0x7f
            n = int.from_bytes(b[j:j + k], "big")
            j += k
        out.append((tag, i, j, j + n))
        i = j + n
    return out


def children(b: bytes, item):
    return der_items(b, item[2], item[3])


def epoch(dt: datetime.datetime) -> str:
    return str(calendar.timegm(dt.timetuple()))


def name_text(name: x509.Name) -> str:
    return name.rfc4514_string()


def general_name_text(g) -> str:
    if isinstance(g, x509.DNSName):
        return "DNS:" + g.value
    if isinstance(g, x509.RFC822Name):
        return "email:" + g.value
    if isinstance(g, x509.UniformResourceIdentifier):
        return "URI:" + g.value
    if isinstance(g, x509.IPAddress):
        v = g.value
        if isinstance(v, (ipaddress.IPv4Network, ipaddress.IPv6Network)):
            return f"IP:{v.network_address}/{v.netmask}"
        return f"IP:{v}"
    if isinstance(g, x509.DirectoryName):
        return "DirName:" + name_text(g.value)
    if isinstance(g, x509.RegisteredID):
        return "RID:" + g.value.dotted_string
    if isinstance(g, x509.OtherName):
        return "othername:" + g.type_id.dotted_string
    raise ValueError(g)


KU_NAMES = ["digital_signature", "content_commitment", "key_encipherment", "data_encipherment", "key_agreement",
            "key_cert_sign", "crl_sign", "encipher_only", "decipher_only"]


def key_usage_bits(ku) -> str:
    bits = []
    for i, name in enumerate(KU_NAMES):
        if name in ("encipher_only", "decipher_only") and not ku.key_agreement:
            continue
        if getattr(ku, name):
            bits.append(str(i))
    return ",".join(bits)


def key_kind(spki_der: bytes):
    """(KeyAlgorithm variant, parameter set) from the algorithm OID, read with openssl."""
    text = subprocess.run(["openssl", "asn1parse", "-inform", "DER"], input=spki_der, check=True,
                          capture_output=True).stdout.decode()
    oids = [line.split(":")[-1].strip() for line in text.splitlines() if "OBJECT" in line]
    table = {"rsaEncryption": ("Rsa", ""), "rsassaPss": ("RsaPss", ""), "ED25519": ("Ed25519", ""),
             "ED448": ("Ed448", ""), "X25519": ("X25519", ""), "X448": ("X448", ""), "ML-DSA-44": ("MlDsa44", ""),
             "ML-DSA-65": ("MlDsa65", ""), "ML-DSA-87": ("MlDsa87", ""), "ML-KEM-512": ("MlKem512", ""),
             "ML-KEM-768": ("MlKem768", ""), "ML-KEM-1024": ("MlKem1024", ""),
             "SLH-DSA-SHA2-128s": ("SlhDsa", "SLH-DSA-SHA2-128s"), "SLH-DSA-SHAKE-128f": ("SlhDsa", "SLH-DSA-SHAKE-128f")}
    curves = {"prime256v1": "P-256", "secp384r1": "P-384", "secp521r1": "P-521", "secp256k1": "secp256k1"}
    if oids[0] == "id-ecPublicKey":
        return "Ec", curves[oids[1]]
    return table[oids[0]]


def certificate_fields(name: str, der: bytes):
    rows = []

    def add(field, value):
        rows.append([name, field, value])

    cert = x509.load_der_x509_certificate(der)
    top = der_items(der)[0]
    tbs_item, _, _ = children(der, top)
    tbs_fields = children(der, tbs_item)
    first = 1 if tbs_fields[0][0] == 0xa0 else 0
    spki_item = tbs_fields[first + 5]
    spki = der[spki_item[1]:spki_item[3]]
    add("der", der.hex())
    add("tbs", cert.tbs_certificate_bytes.hex())
    add("version", str(cert.version.value + 1))
    serial = cert.serial_number
    add("serial", serial.to_bytes((serial.bit_length() + 8) // 8, "big", signed=True).hex())
    add("issuer", name_text(cert.issuer))
    add("subject", name_text(cert.subject))
    add("not_before", epoch(cert.not_valid_before_utc))
    add("not_after", epoch(cert.not_valid_after_utc))
    add("spki", spki.hex())
    kind, param_set = key_kind(spki)
    add("key_algorithm", kind)
    add("parameter_set", param_set)
    add("signature_algorithm", cert.signature_algorithm_oid.dotted_string)
    add("signature", cert.signature.hex())
    exts = []
    for ext in cert.extensions:
        exts.append(ext.oid.dotted_string + (":c" if ext.critical else ":n"))
        v = ext.value
        if isinstance(v, x509.BasicConstraints):
            add("basic_constraints", f"{str(v.ca).lower()},{-1 if v.path_length is None else v.path_length}")
        elif isinstance(v, x509.KeyUsage):
            add("key_usage", key_usage_bits(v))
        elif isinstance(v, x509.ExtendedKeyUsage):
            add("extended_key_usage", ",".join(o.dotted_string for o in v))
        elif isinstance(v, x509.SubjectAlternativeName):
            add("subject_alt_names", "|".join(general_name_text(g) for g in v))
        elif isinstance(v, x509.NameConstraints):
            add("permitted", "|".join(general_name_text(g) for g in (v.permitted_subtrees or [])))
            add("excluded", "|".join(general_name_text(g) for g in (v.excluded_subtrees or [])))
        elif isinstance(v, x509.AuthorityKeyIdentifier):
            add("authority_key_identifier", v.key_identifier.hex())
        elif isinstance(v, x509.SubjectKeyIdentifier):
            add("subject_key_identifier", v.digest.hex())
        elif isinstance(v, x509.CertificatePolicies):
            parts = []
            for info in v:
                texts = []
                for q in info.policy_qualifiers or []:
                    texts.append(q if isinstance(q, str) else (q.explicit_text or ""))
                parts.append(info.policy_identifier.dotted_string + ";" + ";".join(texts))
            add("policies", "|".join(parts))
        elif isinstance(v, x509.PolicyConstraints):
            r = -1 if v.require_explicit_policy is None else v.require_explicit_policy
            i = -1 if v.inhibit_policy_mapping is None else v.inhibit_policy_mapping
            add("policy_constraints", f"{r},{i}")
        elif isinstance(v, x509.InhibitAnyPolicy):
            add("inhibit_any_policy", str(v.skip_certs))
        elif isinstance(v, x509.CRLDistributionPoints):
            add("crl_distribution_points", "|".join(general_name_text(g) for p in v for g in (p.full_name or [])))
        elif isinstance(v, x509.AuthorityInformationAccess):
            add("authority_info_access", "|".join(d.access_method.dotted_string + "=" + general_name_text(d.access_location)
                                                  for d in v))
        elif ext.oid.dotted_string == "2.5.29.33":
            # cryptography 43 keeps policyMappings unparsed: read the pairs with openssl.
            text = subprocess.run(["openssl", "x509", "-inform", "DER", "-noout", "-ext", "policyMappings"], input=der,
                                  check=True, capture_output=True).stdout.decode()
            pairs = [line.strip() for line in text.splitlines()[1:] if ":" in line]
            add("policy_mappings", "|".join(p.replace(":", "->") for p in pairs))
    add("extensions", "|".join(exts))
    return rows


def spki_rows(material: pathlib.Path):
    rows = []
    for path in sorted((material / "certs").glob("zoo-*.spki.der")):
        name = path.name[len("zoo-"):-len(".spki.der")]
        der = path.read_bytes()
        top = der_items(der)[0]
        _, key_item = children(der, top)
        key = der[key_item[2] + 1:key_item[3]]
        kind, param_set = key_kind(der)
        modulus = exponent = ""
        if kind in ("Rsa", "RsaPss"):
            numbers = serialization.load_der_public_key(der).public_numbers()
            modulus = numbers.n.to_bytes((numbers.n.bit_length() + 7) // 8, "big").hex()
            exponent = numbers.e.to_bytes((numbers.e.bit_length() + 7) // 8, "big").hex()
        rows.append([name, der.hex(), kind, param_set, key.hex(), modulus, exponent])
    return rows


def pkcs8_rows(material: pathlib.Path):
    rows = []
    for path in sorted((material / "certs").glob("zoo-*.p8.der")):
        name = path.name[len("zoo-"):-len(".p8.der")]
        der = path.read_bytes()
        top = der_items(der)[0]
        fields = children(der, top)
        version = der[fields[0][2]]
        alg_oid_item = children(der, fields[1])[0]
        oid_der = der[alg_oid_item[1]:alg_oid_item[3]]
        private = der[fields[2][2]:fields[2][3]]
        public = ""
        for f in fields[3:]:
            if f[0] == 0x81:
                public = der[f[2] + 1:f[3]].hex()
        rows.append([name, der.hex(), str(version), oid_der.hex(), private.hex(), public])
    return rows


def csr_rows(material: pathlib.Path):
    rows = []
    for name in ["csr-p256", "csr-ed25519"]:
        der = (material / "certs" / f"{name}.der").read_bytes()
        csr = x509.load_der_x509_csr(der)
        top = der_items(der)[0]
        info = children(der, top)[0]
        spki_item = children(der, info)[2]

        def add(field, value):
            rows.append([name, field, value])

        add("der", der.hex())
        add("info", der[info[1]:info[3]].hex())
        add("subject", name_text(csr.subject))
        add("spki", der[spki_item[1]:spki_item[3]].hex())
        add("signature_algorithm", csr.signature_algorithm_oid.dotted_string)
        add("signature", csr.signature.hex())
        sans = ""
        for ext in csr.extensions:
            if isinstance(ext.value, x509.SubjectAlternativeName):
                sans = "|".join(general_name_text(g) for g in ext.value)
        add("subject_alt_names", sans)
        add("extension_count", str(len(csr.extensions)))
        password = ""
        try:
            password = csr.attributes.get_attribute_for_oid(x509.oid.AttributeOID.CHALLENGE_PASSWORD).value.decode()
        except x509.AttributeNotFound:
            pass
        add("challenge_password", password)
        add("valid", str(csr.is_signature_valid).lower())
    return rows


PBES2_OIDS = {"aes-128-cbc": "2.16.840.1.101.3.4.1.2", "aes-192-cbc": "2.16.840.1.101.3.4.1.22",
              "aes-256-cbc": "2.16.840.1.101.3.4.1.42", "aes-128-gcm": "2.16.840.1.101.3.4.1.6",
              "aes-192-gcm": "2.16.840.1.101.3.4.1.26", "aes-256-gcm": "2.16.840.1.101.3.4.1.46",
              "sha224": "1.2.840.113549.2.8", "sha256": "1.2.840.113549.2.9", "sha384": "1.2.840.113549.2.10",
              "sha512": "1.2.840.113549.2.11"}


def oid_der(dotted: str) -> bytes:
    return genstr("OID:" + dotted)


def int_der(value: int) -> bytes:
    return genstr(f"INTEGER:{value}")


def pbes2_encrypt(plain: bytes, password: bytes, salt: bytes, iterations: int, prf: str, cipher: str, iv: bytes) -> bytes:
    """PBES2 (RFC 8018) with PBKDF2 and AES-CBC or AES-GCM (RFC 5084), DER by hand from the RFCs' ASN.1."""
    from cryptography.hazmat.primitives import padding
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    size = int(cipher.split("-")[1]) // 8
    key = hashlib.pbkdf2_hmac(prf, password, salt, iterations, size)
    if cipher.endswith("gcm"):
        data = AESGCM(key).encrypt(iv, plain, None)
        params = tlv(0x30, tlv(0x04, iv) + int_der(16))
    else:
        padder = padding.PKCS7(128).padder()
        padded = padder.update(plain) + padder.finalize()
        enc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
        data = enc.update(padded) + enc.finalize()
        params = tlv(0x04, iv)
    kdf = tlv(0x30, oid_der("1.2.840.113549.1.5.12") + tlv(0x30, tlv(0x04, salt) + int_der(iterations)
                                                         + tlv(0x30, oid_der(PBES2_OIDS[prf]) + b"\x05\x00")))
    scheme = tlv(0x30, oid_der(PBES2_OIDS[cipher]) + params)
    alg = tlv(0x30, oid_der("1.2.840.113549.1.5.13") + tlv(0x30, kdf + scheme))
    return tlv(0x30, alg + tlv(0x04, data))


def pbes2_decrypt_openssl_record(der: bytes, password: bytes) -> bytes:
    """Decrypts an `openssl pkcs8 -v2` record with hashlib and cryptography, reading the parameters by hand."""
    from cryptography.hazmat.primitives import padding
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    top = der_items(der)[0]
    alg, data_item = children(der, top)
    _, params = children(der, alg)
    kdf, scheme = children(der, params)
    _, kdf_params = children(der, kdf)
    kp = children(der, kdf_params)
    salt = der[kp[0][2]:kp[0][3]]
    iterations = int.from_bytes(der[kp[1][2]:kp[1][3]], "big")
    prf_items = [item for item in kp[2:] if item[0] == 0x30]
    if not prf_items:
        return b""  # The default PRF, HMAC-SHA-1: unsupported by Std.Crypto.Pki.
    prf_oid = der[children(der, prf_items[0])[0][1]:children(der, prf_items[0])[0][3]]
    prf = [k for k, v in PBES2_OIDS.items() if oid_der(v) == prf_oid]
    if not prf:
        return b""
    scheme_oid, iv_item = children(der, scheme)
    cipher = [k for k, v in PBES2_OIDS.items() if oid_der(v) == der[scheme_oid[1]:scheme_oid[3]]][0]
    iv = der[iv_item[2]:iv_item[3]]
    key = hashlib.pbkdf2_hmac(prf[0], password, salt, iterations, int(cipher.split("-")[1]) // 8)
    dec = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    padded = dec.update(der[data_item[2]:data_item[3]]) + dec.finalize()
    unpad = padding.PKCS7(128).unpadder()
    return unpad.update(padded) + unpad.finalize()


def encrypted_rows(material: pathlib.Path):
    password = b"correct horse"
    rows = []
    for path in sorted((material / "encrypted").glob("*.der")):
        der = path.read_bytes()
        rows.append(["openssl " + path.stem, der.hex(), password.decode(), pbes2_decrypt_openssl_record(der, password).hex()])
    plain = (material / "certs" / "zoo-ed25519.p8.der").read_bytes()
    built = []
    for cipher, prf, iterations in [("aes-256-gcm", "sha256", 100), ("aes-128-gcm", "sha384", 50),
                                    ("aes-192-gcm", "sha224", 10), ("aes-128-cbc", "sha256", 20),
                                    ("aes-256-cbc", "sha512", 3)]:
        salt = bytes(range(1, 17))
        iv = bytes(range(100, 112 if cipher.endswith("gcm") else 116))
        der = pbes2_encrypt(plain, password, salt, iterations, prf, cipher, iv)
        rows.append([f"python {cipher} {prf}", der.hex(), password.decode(), plain.hex()])
        built.append([PBES2_OIDS[cipher], PBES2_OIDS[prf], salt.hex(), iv.hex(), str(iterations), password.decode(),
                      plain.hex(), der.hex()])
    return rows, built


def pem_lines(label: str, der: bytes) -> str:
    """PEM text from `openssl` itself, with '|' for each line feed (cells may not hold newlines)."""
    proc = subprocess.run(["openssl", "x509", "-inform", "DER", "-outform", "PEM"], input=der, check=True,
                          capture_output=True)
    assert label == "CERTIFICATE"
    return proc.stdout.decode().replace("\n", "|")


def main() -> int:
    material = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else MATERIAL
    certs = material / "certs"
    oid_rows, int_rows, time_rows, string_rows, bad, bad_value = der_tables()
    cert_rows = []
    pem_rows = []
    for name in ["root", "inter", "leaf"]:
        der = (certs / f"{name}.der").read_bytes()
        cert_rows.append([name, der.hex()])
        pem_rows.append([name, pem_lines("CERTIFICATE", der)])
    cert_names = ["root", "inter", "leaf", "neg-wildcard", "neg-critical", "sub-ca", "policy-inter", "policy-leaf-any",
                  "rsa-root", "rsa-pss-leaf", "mldsa-root"]
    field_rows = []
    for name in cert_names:
        field_rows += certificate_fields(name, (certs / f"{name}.der").read_bytes())
    print(write_module(
        "crypto-pki", "cert-vectors.nra", "CertVectors",
        ["python3 scripts/crypto-vectors/gen-pki.py: fields of the recorded openssl material read with",
         f"Python cryptography {cryptography.__version__} (OIDs of key algorithms and policyMappings with openssl 3.5.7)."],
        [("certificates", ["name", "field", "expected value"], field_rows),
         ("public_keys", ["name", "SPKI DER", "KeyAlgorithm", "parameter set", "key bytes", "RSA modulus",
                          "RSA exponent"], spki_rows(material)),
         ("csrs", ["name", "field", "expected value"], csr_rows(material))]))
    encrypted, built = encrypted_rows(material)
    print(write_module(
        "crypto-pki", "pkcs8-vectors.nra", "Pkcs8Vectors",
        ["python3 scripts/crypto-vectors/gen-pki.py: OneAsymmetricKey files from `openssl pkey` (recorded material),",
         "PBES2 records from `openssl pkcs8 -v2` decrypted with hashlib.pbkdf2_hmac and cryptography's AES-CBC,",
         f"and PBES2 with AES-GCM (RFC 5084) or AES-CBC built with hashlib and cryptography {cryptography.__version__}."],
        [("private_keys", ["name", "PKCS#8 DER", "version", "algorithm OID DER", "privateKey contents",
                           "publicKey bytes"], pkcs8_rows(material)),
         ("encrypted", ["source", "EncryptedPrivateKeyInfo DER", "password", "plaintext PKCS#8 (empty: unsupported PRF)"],
          encrypted),
         ("built", ["cipher OID", "PRF OID", "salt", "IV or nonce", "iterations", "password", "plaintext",
                    "expected DER"], built)]))
    print(write_module(
        "crypto-pki", "der-vectors.nra", "DerVectors",
        ["python3 scripts/crypto-vectors/gen-pki.py: encodings from `openssl asn1parse -genstr` (OpenSSL 3.5.7),",
         "certificates from the recorded material of gen-pki-material.py, malformed inputs built per X.690 rule."],
        [("oids", ["dotted", "DER"], oid_rows),
         ("integers", ["decimal value", "DER"], int_rows),
         ("times", ["DER", "epoch seconds"], time_rows),
         ("strings", ["DER", "UTF-8 of the text"], string_rows),
         ("malformed", ["input", "error offset", "rule"], bad),
         ("malformed_values", ["reader", "element", "error offset", "rule"], bad_value),
         ("certificates", ["name", "DER"], cert_rows),
         ("pem", ["name", "openssl PEM output with | for line feeds"], pem_rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
