#!/usr/bin/env python3
"""Path-validation vectors for Std.Crypto.Pki.

Two outputs:
- path-vectors.nra: the recorded openssl chains of gen-pki-material.py (DER), the signature truth table for
  every (certificate, candidate issuer) pair among them, and policy-processing cases whose expected outcome
  is `openssl verify` 3.5.7 with -policy_check and the matching -policy/-explicit_policy/-inhibit_map/
  -inhibit_any flags (an independent RFC 5280 policy-tree implementation).
- limbo-<one|two|...>-vectors.nra: selected cases of the C2SP x509-limbo suite (limbo.json at commit
  b12423782a88abead01141fa6582254e3eb49e74, recorded in sources/pki/SOURCES.txt), with each case's
  certificates, inputs and expected result, and the signature truth table for every certificate and
  candidate issuer of the case.

Truth table: key = SHA-256(issuer SPKI DER || TBS bytes || signature), value "valid" or "invalid", computed with
Python `cryptography` (RSA PKCS#1 v1.5 and PSS, ECDSA, Ed25519, Ed448) or, for ML-DSA, `openssl pkeyutl -verify`.
A pair whose algorithm neither supports is left out, so the test double answers UnsupportedAlgorithm.

    python3 scripts/crypto-vectors/gen-pki-paths.py [sources-dir]
"""

import datetime
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

import cryptography
from cryptography import x509
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed448, ed25519, padding, rsa

from nra_vectors import write_module

SOURCES = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/pki")

# Limbo cases: id prefixes taken whole, and the ids of the bettertls name-constraint sample.
PREFIXES = ["rfc5280::", "pathlen::", "pathological::", "webpki::", "cve::", "invalid::", "rfc9881::"]

# Cases where this library's documented rules differ from limbo's expectation: id -> (our result, reason).
DIVERGENCES = {
    "webpki::eku::ee-without-eku": ("SUCCESS", "RFC 5280: a leaf without extKeyUsage is not restricted (webpki-only rule)"),
    "webpki::eku::ee-critical-eku": ("SUCCESS", "RFC 5280 allows a critical extKeyUsage (webpki-only rule)"),
    "webpki::eku::ee-anyeku": ("SUCCESS", "RFC 5280: anyExtendedKeyUsage allows every purpose (webpki-only rule)"),
    "webpki::eku::root-has-eku": ("SUCCESS", "a trust anchor's extKeyUsage is not checked"),
    "webpki::nc::permitted-dns-match-noncritical": ("FAILURE", "strict profile: nameConstraints must be critical (RFC 5280)"),
    "webpki::san::public-suffix-multi-label-wildcard-san": ("SUCCESS", "no public-suffix list"),
    "webpki::san::public-suffix-private-namespace-wildcard-san": ("SUCCESS", "no public-suffix list"),
    "webpki::forbidden-rsa-not-divisible-by-8-in-root": ("SUCCESS", "RSA moduli are not required to be a multiple of 8 bits"),
    "webpki::forbidden-rsa-key-not-divisible-by-8-in-leaf": ("SUCCESS", "RSA moduli are not required to be a multiple of 8 bits"),
    "webpki::ca-as-leaf": ("SUCCESS", "RFC 5280 allows a CA certificate as the leaf (rfc5280::ca-as-leaf)"),
    "webpki::ee-basicconstraints-ca": ("SUCCESS", "RFC 5280 allows a CA certificate as the leaf"),
    "webpki::san::san-critical-with-nonempty-subject": ("SUCCESS", "RFC 5280 says SHOULD NOT, not MUST NOT"),
    "webpki::aki::root-with-aki-all-fields": ("SUCCESS", "RFC 5280 allows authorityCertIssuer and serial in AKI"),
    "webpki::cn::ipv4-hex-mismatch": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "webpki::cn::ipv4-leading-zeros-mismatch": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "webpki::cn::ipv6-uppercase-mismatch": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "webpki::cn::ipv6-uncompressed-mismatch": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "webpki::cn::ipv6-non-rfc5952-mismatch": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "webpki::cn::punycode-not-in-san": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "webpki::cn::utf8-vs-punycode-mismatch": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "webpki::cn::not-in-san": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "webpki::cn::case-mismatch": ("SUCCESS", "the subject commonName is not checked (CA/B Forum rule)"),
    "rfc5280::aki::cross-signed-root-missing-aki": ("SUCCESS", "a trust anchor needs no authorityKeyIdentifier (cve::cve-2024-0567 needs one without)"),
    "webpki::forbidden-dsa-leaf": ("SUCCESS", "a leaf's own key algorithm is not restricted (no subscriber-key policy)"),
}


def epoch(text: str) -> str:
    return str(int(datetime.datetime.fromisoformat(text).timestamp()))


def verify(issuer: x509.Certificate, child: x509.Certificate):
    """True, False, or None when neither `cryptography` nor `openssl` can check the pair."""
    try:
        key = issuer.public_key()
    except Exception:
        key = None
    if key is not None:
        try:
            if isinstance(key, rsa.RSAPublicKey):
                params = child.signature_algorithm_parameters
                if isinstance(params, padding.PSS):
                    key.verify(child.signature, child.tbs_certificate_bytes, params, child.signature_hash_algorithm)
                elif isinstance(params, padding.PKCS1v15):
                    key.verify(child.signature, child.tbs_certificate_bytes, params, child.signature_hash_algorithm)
                else:
                    return False
            elif isinstance(key, ec.EllipticCurvePublicKey):
                if child.signature_hash_algorithm is None:
                    return False
                key.verify(child.signature, child.tbs_certificate_bytes, ec.ECDSA(child.signature_hash_algorithm))
            elif isinstance(key, (ed25519.Ed25519PublicKey, ed448.Ed448PublicKey)):
                key.verify(child.signature, child.tbs_certificate_bytes)
            else:
                return None
            return True
        except InvalidSignature:
            return False
        except Exception:
            return False
    # ML-DSA and other keys cryptography 43 cannot load: openssl.
    with tempfile.TemporaryDirectory() as d:
        dp = pathlib.Path(d)
        (dp / "issuer.pem").write_bytes(issuer.public_bytes(serialization.Encoding.PEM))
        pub = subprocess.run(["openssl", "x509", "-in", str(dp / "issuer.pem"), "-pubkey", "-noout"], capture_output=True)
        if pub.returncode != 0:
            return None
        (dp / "pub.pem").write_bytes(pub.stdout)
        (dp / "tbs").write_bytes(child.tbs_certificate_bytes)
        (dp / "sig").write_bytes(child.signature)
        r = subprocess.run(["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", str(dp / "pub.pem"), "-rawin", "-in",
                            str(dp / "tbs"), "-sigfile", str(dp / "sig")], capture_output=True)
        out = r.stdout.decode() + r.stderr.decode()
        if "Signature Verified Successfully" in out:
            return True
        if "Signature Verification Failure" in out:
            return False
        return None


def table_rows(certs, issuers):
    rows = {}
    for child in certs:
        for issuer in issuers:
            if child.issuer.public_bytes() != issuer.subject.public_bytes() and child.issuer != issuer.subject:
                continue
            ok = verify(issuer, child)
            if ok is None:
                continue
            spki = issuer.public_key_bytes if hasattr(issuer, "public_key_bytes") else None
            spki = spki_der(issuer)
            key = hashlib.sha256(spki + child.tbs_certificate_bytes + child.signature).hexdigest()
            rows[key] = "valid" if ok else "invalid"
    return rows


def spki_der(cert: x509.Certificate) -> bytes:
    der = cert.public_bytes(serialization.Encoding.DER)
    # Certificate -> TBSCertificate -> fields; the SubjectPublicKeyInfo follows the subject.
    def items(b, start, end):
        out = []
        i = start
        while i < end:
            n = b[i + 1]
            j = i + 2
            if n & 0x80:
                k = n & 0x7f
                n = int.from_bytes(b[j:j + k], "big")
                j += k
            out.append((b[i], i, j, j + n))
            i = j + n
        return out
    top = items(der, 0, len(der))[0]
    tbs = items(der, top[2], top[3])[0]
    fields = items(der, tbs[2], tbs[3])
    first = 1 if fields[0][0] == 0xa0 else 0
    s = fields[first + 5]
    return der[s[1]:s[3]]


POLICY_CASES = [
    # name, leaf, intermediates, user policies, explicit, inhibit mapping, inhibit any
    ("mapped policy, any user policy", "policy-leaf", ["policy-inter"], [], False, False, False),
    ("mapped policy, issuer-domain user policy", "policy-leaf", ["policy-inter"], ["1.3.6.1.4.1.55555.2.1"], True, False, False),
    ("mapped policy, other user policy", "policy-leaf", ["policy-inter"], ["1.3.6.1.4.1.55555.2.9"], True, False, False),
    ("mapping inhibited", "policy-leaf", ["policy-inter"], [], False, True, False),
    ("mapping inhibited, explicit", "policy-leaf", ["policy-inter"], [], True, True, False),
    ("anyPolicy leaf, inhibited by the intermediate", "policy-leaf-any", ["policy-inter"], [], False, False, False),
    ("anyPolicy leaf, explicit user policy", "policy-leaf-any", ["policy-inter"], ["1.3.6.1.4.1.55555.2.1"], True, False, False),
    ("any inhibited from the start", "policy-leaf", ["policy-inter"], [], True, False, True),
    ("anyPolicy CA, requireExplicitPolicy 1, two mappings to one policy", "px-leaf", ["px-ca2", "px-ca1"], [], False, False, False),
    ("two mappings, user policy 1.2.3.2", "px-leaf", ["px-ca2", "px-ca1"], ["1.2.3.2"], False, False, False),
    ("two mappings, user policy 1.2.3.9", "px-leaf", ["px-ca2", "px-ca1"], ["1.2.3.9"], False, False, False),
    ("two mappings, mapping inhibited", "px-leaf", ["px-ca2", "px-ca1"], [], False, True, False),
    ("anyPolicy CA with any inhibited", "px-leaf", ["px-ca2", "px-ca1"], [], False, False, True),
    ("inhibitPolicyMapping 1: second mapping level", "py-leaf", ["py-ca3", "py-ca2", "py-ca1"], [], False, False, False),
    ("inhibitPolicyMapping 1: second level, explicit", "py-leaf", ["py-ca3", "py-ca2", "py-ca1"], [], True, False, False),
    ("inhibitPolicyMapping 1: first level only", "py-leaf-mid", ["py-ca2", "py-ca1"], [], True, False, False),
    ("inhibitAnyPolicy 1 then anyPolicy CA", "pz-leaf", ["pz-ca2", "pz-ca1"], [], True, False, False),
    ("inhibitAnyPolicy 0 then anyPolicy CA", "pz-leaf", ["pz-ca2", "pz-ca1-zero"], [], True, False, False),
    ("inhibitAnyPolicy 0, not explicit", "pz-leaf", ["pz-ca2", "pz-ca1-zero"], [], False, False, False),
    ("self-issued rollover keeps anyPolicy", "pz-rollover-leaf", ["pz-rollover", "pz-ca1-zero"], [], True, False, False),
    ("self-issued rollover, user policy", "pz-rollover-leaf", ["pz-rollover", "pz-ca1-zero"], ["1.2.5.5"], True, False, False),
]


def openssl_policy_result(material: pathlib.Path, leaf: str, intermediates, policies, explicit, inhibit_map,
                          inhibit_any) -> str:
    with tempfile.TemporaryDirectory() as d:
        dp = pathlib.Path(d)
        for name in ["policy-root", leaf] + intermediates:
            subprocess.run(["openssl", "x509", "-inform", "DER", "-in", str(material / "certs" / f"{name}.der"), "-out",
                            str(dp / f"{name}.pem")], check=True)
        (dp / "untrusted.pem").write_text("".join((dp / f"{n}.pem").read_text() for n in intermediates))
        args = ["openssl", "verify", "-policy_check", "-attime", "1780000000", "-CAfile", str(dp / "policy-root.pem"),
                "-untrusted", str(dp / "untrusted.pem")]
        # The openssl command line's user-initial-policy-set is empty unless -policy is given; RFC 5280's default,
        # which Std.Crypto.Pki uses, is {anyPolicy}.
        for p in policies or ["2.5.29.32.0"]:
            args += ["-policy", p]
        if explicit:
            args.append("-explicit_policy")
        if inhibit_map:
            args.append("-inhibit_map")
        if inhibit_any:
            args.append("-inhibit_any")
        r = subprocess.run(args + [str(dp / f"{leaf}.pem")], capture_output=True)
        return "valid" if r.returncode == 0 else "invalid"


def material_vectors(sources: pathlib.Path):
    material = sources / "material"
    names = ["root", "inter", "leaf", "neg-excluded-dns", "neg-outside-dns", "neg-excluded-ip", "neg-outside-ipv6",
             "neg-outside-email", "neg-outside-uri", "neg-outside-dirname", "neg-expired", "neg-wildcard",
             "neg-codesign", "neg-critical", "sub-ca", "sub-leaf", "no-kcs-ca", "no-kcs-leaf", "policy-root",
             "policy-inter", "policy-leaf", "policy-leaf-any", "rsa-root", "rsa-pss-leaf", "px-ca1", "px-ca2", "px-leaf",
             "py-ca1", "py-ca2", "py-ca3", "py-leaf", "py-leaf-mid", "pz-ca1", "pz-ca1-zero", "pz-ca2", "pz-leaf",
             "pz-rollover", "pz-rollover-leaf"]
    ders = {n: (material / "certs" / f"{n}.der").read_bytes() for n in names}
    certs = {n: x509.load_der_x509_certificate(d) for n, d in ders.items()}
    table = table_rows(list(certs.values()), list(certs.values()))
    policy_rows = []
    for case, leaf, intermediates, policies, explicit, inhibit_map, inhibit_any in POLICY_CASES:
        expected = openssl_policy_result(material, leaf, intermediates, policies, explicit, inhibit_map, inhibit_any)
        policy_rows.append([case, leaf, ",".join(intermediates), ",".join(policies), str(explicit).lower(),
                            str(inhibit_map).lower(), str(inhibit_any).lower(), expected])
    return ([[n, d.hex()] for n, d in ders.items()], [[k, v] for k, v in sorted(table.items())], policy_rows)


def pem_ders(text: str):
    out = []
    for block in text.split("-----END CERTIFICATE-----"):
        if "-----BEGIN CERTIFICATE-----" in block:
            pem = block[block.index("-----BEGIN CERTIFICATE-----"):] + "-----END CERTIFICATE-----\n"
            out.append(x509.load_pem_x509_certificate(pem.encode()))
    return out


def limbo_vectors(sources: pathlib.Path):
    data = json.loads((sources / "limbo.json").read_text())
    cases = []
    for case in data["testcases"]:
        cid = case["id"]
        if not any(cid.startswith(p) for p in PREFIXES):
            continue
        cases.append(case)
    case_rows, cert_rows, sig_rows, skipped = [], [], [], []
    for case in cases:
        cid = case["id"]
        try:
            peer = pem_ders(case["peer_certificate"])
            inter = [c for p in case["untrusted_intermediates"] for c in pem_ders(p)]
            trusted = [c for p in case["trusted_certs"] for c in pem_ders(p)]
        except Exception as e:  # A certificate cryptography cannot load (malformed on purpose): keep the bytes.
            peer = inter = trusted = None
        raw_peer = [case["peer_certificate"]]
        def raw(pems):
            out = []
            for p in pems:
                for block in p.split("-----END CERTIFICATE-----"):
                    if "-----BEGIN CERTIFICATE-----" in block:
                        b64 = "".join(block[block.index("-----BEGIN CERTIFICATE-----") + 27:].split())
                        import base64
                        out.append(base64.b64decode(b64))
            return out
        peer_der = raw(raw_peer)
        inter_der = raw(case["untrusted_intermediates"])
        trusted_der = raw(case["trusted_certs"])
        name = case["expected_peer_name"] or {}
        names = case["expected_peer_names"] or []
        if not name and names:
            name = names[0]
        ours, why = DIVERGENCES.get(cid, (case["expected_result"], ""))
        cases_eku = ",".join({"serverAuth": "1.3.6.1.5.5.7.3.1", "clientAuth": "1.3.6.1.5.5.7.3.2"}.get(e, e)
                              for e in case["extended_key_usage"])
        case_rows.append([cid, case["expected_result"], ours, why, name.get("kind", ""), name.get("value", ""),
                          epoch(case["validation_time"]) if case["validation_time"] else "",
                          str(case["max_chain_depth"]) if case["max_chain_depth"] is not None else "-1", cases_eku,
                          str(len(inter_der)), str(len(trusted_der))])
        for role, ders in [("peer", peer_der), ("intermediate", inter_der), ("trusted", trusted_der)]:
            for d in ders:
                cert_rows.append([cid, role, d.hex()])
        loaded = []
        for d in peer_der + inter_der + trusted_der:
            try:
                loaded.append(x509.load_der_x509_certificate(d))
            except Exception:
                pass
        issuers = []
        for d in inter_der + trusted_der:
            try:
                issuers.append(x509.load_der_x509_certificate(d))
            except Exception:
                pass
        if len(loaded) > 50:
            # The 100-certificate pathological pools: no table rows (10,000 pairs); every edge then reads as
            # UnsupportedAlgorithm, and the search must still stop within its budget and fail.
            continue
        for k, v in sorted(table_rows(loaded, issuers).items()):
            sig_rows.append([cid, k, v])
    return case_rows, cert_rows, sig_rows


def main() -> int:
    sources = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    cert_rows, table, policy_rows = material_vectors(sources)
    print(write_module(
        "crypto-pki", "path-vectors.nra", "PathVectors",
        ["python3 scripts/crypto-vectors/gen-pki-paths.py: recorded openssl chains (gen-pki-material.py), their",
         f"signature truth table from Python cryptography {cryptography.__version__}, and policy cases decided by",
         "`openssl verify -policy_check` (OpenSSL 3.5.7) at 1780000000 (2026-05-28T20:26:40Z)."],
        [("certificates", ["name", "DER"], cert_rows),
         ("signatures", ["SHA-256(SPKI || TBS || signature)", "valid or invalid"], table),
         ("policy_cases", ["case", "leaf", "intermediates", "user policies", "explicit", "inhibit mapping", "inhibit any",
                           "openssl verify result"], policy_rows)]))
    case_rows, limbo_certs, limbo_sigs = limbo_vectors(sources)
    # Split the cases over files of about 60 cases, so each test file stays within its time budget.
    per_file = 60
    for index in range(0, len(case_rows), per_file):
        chunk = case_rows[index:index + per_file]
        ids = {row[0] for row in chunk}
        part = ["one", "two", "three", "four", "five", "six"][index // per_file]
        print(write_module(
            "crypto-pki", f"limbo-{part}-vectors.nra", f"Limbo{part.capitalize()}Vectors",
            ["python3 scripts/crypto-vectors/gen-pki-paths.py: C2SP x509-limbo limbo.json at commit",
             "b12423782a88abead01141fa6582254e3eb49e74 (cases with the prefixes " + ", ".join(PREFIXES) + "),",
             f"signature truth table from Python cryptography {cryptography.__version__} and openssl 3.5.7 (ML-DSA)."],
            [("cases", ["id", "limbo expected", "expected here", "reason for a difference", "peer name kind",
                        "peer name", "validation time", "max chain depth", "extended key usages",
                        "intermediates", "trusted"], chunk),
             ("certificates", ["id", "role", "DER"], [r for r in limbo_certs if r[0] in ids]),
             ("signatures", ["id", "SHA-256(SPKI || TBS || signature)", "valid or invalid"],
              [r for r in limbo_sigs if r[0] in ids])]))
    print(len(case_rows), "limbo cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
