#!/usr/bin/env python3
"""Records the openssl material behind the Std.Crypto.Pki tests.

Runs the `openssl` 3.5.7 command line to make test keys, certificates, CSRs and
PKCS#8 files under the recorded-material directory (default
/home/dev/neura-gate/030/crypto2/sources/pki/material, or the first argument).
Keys are made once and kept; certificates are then made again from them with
fixed serials, fixed validity dates and deterministic signatures (ECDSA with
RFC 6979 nonces via `-sigopt nonce-type:1`, Ed25519, RSA PKCS#1 v1.5, ML-DSA with `deterministic:1`), so a
re-run with the kept keys gives the same certificates. RSA-PSS signatures and
encrypted PKCS#8 files are randomized; the material directory is the record,
and gen-pki.py only transcodes it.

    python3 scripts/crypto-vectors/gen-pki-material.py [material-dir]
"""

import os
import pathlib
import shutil
import subprocess
import sys

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/pki/material")

# Key name -> genpkey arguments.
KEYS = {
    "root": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256"],
    "inter": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-384"],
    "leaf": ["-algorithm", "ED25519"],
    "leaf2": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256"],
    "ocsp": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256"],
    "policy-root": ["-algorithm", "ED25519"],
    "policy-inter": ["-algorithm", "ED25519"],
    "policy-leaf": ["-algorithm", "ED25519"],
    "rsa-root": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048"],
    "pol-1": ["-algorithm", "ED25519"],
    "pol-2": ["-algorithm", "ED25519"],
    "pol-3": ["-algorithm", "ED25519"],
    "pol-4": ["-algorithm", "ED25519"],
    "mldsa-root": ["-algorithm", "ML-DSA-44"],
    # The SubjectPublicKeyInfo and PKCS#8 zoo.
    "zoo-rsa2048": ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048"],
    "zoo-rsapss": ["-algorithm", "RSA-PSS", "-pkeyopt", "rsa_keygen_bits:2048", "-pkeyopt",
                   "rsa_pss_keygen_md:sha256", "-pkeyopt", "rsa_pss_keygen_mgf1_md:sha256", "-pkeyopt",
                   "rsa_pss_keygen_saltlen:32"],
    "zoo-p256": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256"],
    "zoo-p384": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-384"],
    "zoo-p521": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-521"],
    "zoo-secp256k1": ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:secp256k1"],
    "zoo-ed25519": ["-algorithm", "ED25519"],
    "zoo-ed448": ["-algorithm", "ED448"],
    "zoo-x25519": ["-algorithm", "X25519"],
    "zoo-x448": ["-algorithm", "X448"],
    "zoo-mldsa44": ["-algorithm", "ML-DSA-44"],
    "zoo-mldsa65": ["-algorithm", "ML-DSA-65"],
    "zoo-mldsa87": ["-algorithm", "ML-DSA-87"],
    "zoo-mlkem512": ["-algorithm", "ML-KEM-512"],
    "zoo-mlkem768": ["-algorithm", "ML-KEM-768"],
    "zoo-mlkem1024": ["-algorithm", "ML-KEM-1024"],
    "zoo-slhdsa-sha2-128s": ["-algorithm", "SLH-DSA-SHA2-128s"],
    "zoo-slhdsa-shake-128f": ["-algorithm", "SLH-DSA-SHAKE-128f"],
}

CONFIG = """
[req]
distinguished_name = dn
[dn]
[nc_dir]
C = US
O = Neura Test
[root]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign, cRLSign
subjectKeyIdentifier = hash
[inter]
basicConstraints = critical, CA:TRUE, pathlen:0
keyUsage = critical, keyCertSign, cRLSign, digitalSignature
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
nameConstraints = critical, permitted;DNS:example.com, permitted;IP:10.0.0.0/255.0.0.0, permitted;IP:2001:db8::/ffff:ffff:0:0:0:0:0:0, permitted;email:example.com, permitted;URI:.example.com, permitted;dirName:nc_dir, excluded;DNS:bad.example.com, excluded;IP:10.66.0.0/255.255.0.0
certificatePolicies = 1.3.6.1.4.1.55555.1.1, 1.3.6.1.4.1.55555.1.2
extendedKeyUsage = serverAuth, clientAuth
crlDistributionPoints = URI:http://crl.example.com/inter.crl
authorityInfoAccess = OCSP;URI:http://ocsp.example.com/, caIssuers;URI:http://ca.example.com/root.der
[leaf]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = serverAuth, clientAuth
subjectAltName = DNS:www.example.com, DNS:*.api.example.com, IP:10.1.2.3, IP:2001:db8::1, email:admin@example.com, URI:https://www.example.com/path
certificatePolicies = @leaf_policy
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
crlDistributionPoints = URI:http://crl.example.com/inter.crl
authorityInfoAccess = OCSP;URI:http://ocsp.example.com/, caIssuers;URI:http://ca.example.com/inter.der
1.3.6.1.4.1.55555.9.1 = ASN1:UTF8String:kept raw
[leaf_policy]
policyIdentifier = 1.3.6.1.4.1.55555.1.1
CPS.1 = "http://cps.example.com/"
userNotice.1 = @leaf_notice
[leaf_notice]
explicitText = "Test policy"
[neg]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = serverAuth
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
subjectAltName = $ENV::NEG_SAN
[neg_codesign]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = codeSigning
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
subjectAltName = DNS:www.example.com
[neg_critical]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = serverAuth
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
subjectAltName = DNS:www.example.com
1.3.6.1.4.1.55555.9.2 = critical, ASN1:NULL
[sub_ca]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
[no_kcs_ca]
basicConstraints = critical, CA:TRUE
keyUsage = critical, digitalSignature
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
[policy_root]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
[policy_inter]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.3.6.1.4.1.55555.2.1, 1.3.6.1.4.1.55555.2.9
policyMappings = 1.3.6.1.4.1.55555.2.1:1.3.6.1.4.1.55555.3.1
policyConstraints = critical, requireExplicitPolicy:0
inhibitAnyPolicy = critical, 0
[policy_leaf]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.3.6.1.4.1.55555.3.1
subjectAltName = DNS:policy.example.com
[policy_leaf_any]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 2.5.29.32.0
subjectAltName = DNS:policy.example.com
[px_ca1]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 2.5.29.32.0
policyConstraints = critical, requireExplicitPolicy:1
[px_ca2]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.2.3.1, 1.2.3.2
policyMappings = 1.2.3.1:1.2.3.3, 1.2.3.2:1.2.3.3
[px_leaf]
basicConstraints = critical, CA:FALSE
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.2.3.3
[py_ca1]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.2.4.1
policyConstraints = critical, inhibitPolicyMapping:1
[py_ca2]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.2.4.1
policyMappings = 1.2.4.1:1.2.4.2
[py_ca3]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.2.4.2
policyMappings = 1.2.4.2:1.2.4.4
[py_leaf]
basicConstraints = critical, CA:FALSE
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.2.4.4
[py_leaf_mid]
basicConstraints = critical, CA:FALSE
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.2.4.2
[pz_ca1]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 2.5.29.32.0
inhibitAnyPolicy = critical, 1
[pz_ca1_zero]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 2.5.29.32.0
inhibitAnyPolicy = critical, 0
[pz_ca2]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 2.5.29.32.0
[pz_leaf]
basicConstraints = critical, CA:FALSE
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
certificatePolicies = 1.2.5.5
[rsa_leaf]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
subjectAltName = DNS:rsa.example.com
authorityKeyIdentifier = keyid:always
subjectKeyIdentifier = hash
[mldsa_root]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign, digitalSignature
subjectKeyIdentifier = hash
subjectAltName = DNS:pq.example.com
[ocsp_responder]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = OCSPSigning
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
[ocsp_no_eku]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = serverAuth
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
[csr_ext]
subjectAltName = DNS:csr.example.com, IP:192.0.2.7
keyUsage = critical, digitalSignature
"""

ATTRS_CONFIG = """
[req]
prompt = no
distinguished_name = dn
attributes = attrs
[dn]
CN = ed.example.com
[attrs]
challengePassword = neura secret
"""

ROOT_DN = "/C=US/O=Neura Test/CN=Neura Test Root"
INTER_DN = "/C=US/O=Neura Test/CN=Neura Test Intermediate"

# Leaves signed by the intermediate: name -> (key, subject, section, NEG_SAN, not_before, not_after).
NEGATIVE = [
    ("neg-excluded-dns", "/C=US/O=Neura Test/CN=bad", "neg", "DNS:bad.example.com", "20250101000000Z", "20300101000000Z"),
    ("neg-outside-dns", "/C=US/O=Neura Test/CN=other", "neg", "DNS:www.other.com", "20250101000000Z", "20300101000000Z"),
    ("neg-excluded-ip", "/C=US/O=Neura Test/CN=ip", "neg", "IP:10.66.1.1", "20250101000000Z", "20300101000000Z"),
    ("neg-outside-ipv6", "/C=US/O=Neura Test/CN=ip6", "neg", "IP:2001:db9::1", "20250101000000Z", "20300101000000Z"),
    ("neg-outside-email", "/C=US/O=Neura Test/CN=mail", "neg", "email:a@other.com", "20250101000000Z", "20300101000000Z"),
    ("neg-outside-uri", "/C=US/O=Neura Test/CN=uri", "neg", "URI:https://www.other.com/", "20250101000000Z", "20300101000000Z"),
    ("neg-outside-dirname", "/C=US/O=Other/CN=www.example.com", "neg", "DNS:www.example.com", "20250101000000Z", "20300101000000Z"),
    ("neg-expired", "/C=US/O=Neura Test/CN=old", "neg", "DNS:old.example.com", "20200101000000Z", "20210101000000Z"),
    ("neg-wildcard", "/C=US/O=Neura Test/CN=wild", "neg", "DNS:*.wild.example.com, DNS:w*.example.com", "20250101000000Z", "20300101000000Z"),
    ("neg-codesign", "/C=US/O=Neura Test/CN=code", "neg_codesign", "DNS:www.example.com", "20250101000000Z", "20300101000000Z"),
    ("neg-critical", "/C=US/O=Neura Test/CN=critical", "neg_critical", "DNS:www.example.com", "20250101000000Z", "20300101000000Z"),
]

ZOO = [k for k in KEYS if k.startswith("zoo-")]


def run(args, cwd, env=None):
    subprocess.run(["openssl"] + args, cwd=cwd, check=True, env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)


def main() -> int:
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    keys = out / "keys"
    keys.mkdir(parents=True, exist_ok=True)
    for name, args in KEYS.items():
        path = keys / f"{name}.pem"
        if not path.exists():
            run(["genpkey"] + args + ["-out", str(path)], out)
    certs = out / "certs"
    # The RSA-PSS signature is randomized: a recorded leaf is kept, not made again.
    kept_pss = (certs / "rsa-pss-leaf.der").read_bytes() if (certs / "rsa-pss-leaf.der").exists() else None
    if certs.exists():
        shutil.rmtree(certs)
    certs.mkdir()
    work = out / "work"
    work.mkdir(exist_ok=True)
    (work / "openssl.cnf").write_text(CONFIG, encoding="utf-8")
    cnf = str(work / "openssl.cnf")
    env = dict(os.environ, NEG_SAN="DNS:unused.example.com")

    def k(name):
        return str(keys / f"{name}.pem")

    def c(name):
        return str(certs / f"{name}.der")

    def self_signed(name, key, subject, section, serial, nb, na, extra=()):
        run(["req", "-new", "-x509", "-config", cnf, "-key", k(key), "-subj", subject, "-extensions", section,
             "-set_serial", serial, "-not_before", nb, "-not_after", na, "-outform", "DER", "-out", c(name)]
            + list(extra), out, env)

    def issued(name, key, subject, section, issuer, issuer_key, serial, nb, na, extra=(), local_env=None):
        csr = str(work / f"{name}.csr")
        run(["req", "-new", "-config", cnf, "-key", k(key), "-subj", subject, "-out", csr], out, local_env or env)
        run(["x509", "-req", "-in", csr, "-CA", c(issuer), "-CAform", "DER", "-CAkey", k(issuer_key),
             "-extfile", cnf, "-extensions", section, "-set_serial", serial, "-not_before", nb, "-not_after", na,
             "-outform", "DER", "-out", c(name)] + list(extra), out, local_env or env)

    ecdsa = ["-sigopt", "nonce-type:1"]
    self_signed("root", "root", ROOT_DN, "root", "1", "20240101000000Z", "20340101000000Z", ecdsa + ["-sha256"])
    issued("inter", "inter", INTER_DN, "inter", "root", "root", "4096", "20240601000000Z", "20330101000000Z",
           ecdsa + ["-sha256"])
    issued("leaf", "leaf", "/C=US/O=Neura Test/CN=www.example.com", "leaf", "inter", "inter",
           "0x0102030405060708090a0b0c0d0e0f1011121314", "20250101000000Z", "20510101000000Z", ecdsa + ["-sha384"])
    for i, (name, subject, section, san, nb, na) in enumerate(NEGATIVE):
        local_env = dict(env, NEG_SAN=san)
        issued(name, "leaf2", subject, section, "inter", "inter", str(100 + i), nb, na, ecdsa + ["-sha384"], local_env)
    # A CA under the pathlen-0 intermediate, and a leaf under it.
    issued("sub-ca", "leaf2", "/C=US/O=Neura Test/CN=Sub CA", "sub_ca", "inter", "inter", "200",
           "20250101000000Z", "20300101000000Z", ecdsa + ["-sha384"])
    issued("sub-leaf", "leaf", "/C=US/O=Neura Test/CN=www.example.com", "neg", "sub-ca", "leaf2", "201",
           "20250101000000Z", "20300101000000Z", ecdsa + ["-sha256"], dict(env, NEG_SAN="DNS:www.example.com"))
    # A CA without keyCertSign under the root, and a leaf under it.
    issued("no-kcs-ca", "leaf2", "/C=US/O=Neura Test/CN=No KCS CA", "no_kcs_ca", "root", "root", "300",
           "20250101000000Z", "20300101000000Z", ecdsa + ["-sha256"])
    issued("no-kcs-leaf", "leaf", "/C=US/O=Neura Test/CN=www.example.com", "neg", "no-kcs-ca", "leaf2", "301",
           "20250101000000Z", "20300101000000Z", ecdsa + ["-sha256"], dict(env, NEG_SAN="DNS:www.example.com"))
    # The policy chain (Ed25519 throughout).
    self_signed("policy-root", "policy-root", "/O=Neura Test/CN=Policy Root", "policy_root", "10",
                "20240101000000Z", "20340101000000Z")
    issued("policy-inter", "policy-inter", "/O=Neura Test/CN=Policy Intermediate", "policy_inter", "policy-root",
           "policy-root", "11", "20240101000000Z", "20340101000000Z")
    issued("policy-leaf", "policy-leaf", "/O=Neura Test/CN=policy.example.com", "policy_leaf", "policy-inter",
           "policy-inter", "12", "20250101000000Z", "20300101000000Z")
    issued("policy-leaf-any", "policy-leaf", "/O=Neura Test/CN=policy.example.com", "policy_leaf_any",
           "policy-inter", "policy-inter", "13", "20250101000000Z", "20300101000000Z")
    # Longer policy chains under the policy root (Ed25519): skip counts, two mapping levels, anyPolicy in
    # intermediates, and a self-issued intermediate (key rollover: same subject, new key).
    pr, nb, na = "policy-root", "20240101000000Z", "20340101000000Z"
    issued("px-ca1", "pol-1", "/O=Neura Test/CN=PX CA1", "px_ca1", pr, pr, "40", nb, na)
    issued("px-ca2", "pol-2", "/O=Neura Test/CN=PX CA2", "px_ca2", "px-ca1", "pol-1", "41", nb, na)
    issued("px-leaf", "pol-3", "/O=Neura Test/CN=px.example.com", "px_leaf", "px-ca2", "pol-2", "42", nb, na)
    issued("py-ca1", "pol-1", "/O=Neura Test/CN=PY CA1", "py_ca1", pr, pr, "50", nb, na)
    issued("py-ca2", "pol-2", "/O=Neura Test/CN=PY CA2", "py_ca2", "py-ca1", "pol-1", "51", nb, na)
    issued("py-ca3", "pol-3", "/O=Neura Test/CN=PY CA3", "py_ca3", "py-ca2", "pol-2", "52", nb, na)
    issued("py-leaf", "pol-4", "/O=Neura Test/CN=py.example.com", "py_leaf", "py-ca3", "pol-3", "53", nb, na)
    issued("py-leaf-mid", "pol-4", "/O=Neura Test/CN=py2.example.com", "py_leaf_mid", "py-ca2", "pol-2", "54", nb, na)
    issued("pz-ca1", "pol-1", "/O=Neura Test/CN=PZ CA1", "pz_ca1", pr, pr, "60", nb, na)
    issued("pz-ca1-zero", "pol-1", "/O=Neura Test/CN=PZ CA1", "pz_ca1_zero", pr, pr, "61", nb, na)
    issued("pz-ca2", "pol-2", "/O=Neura Test/CN=PZ CA2", "pz_ca2", "pz-ca1", "pol-1", "62", nb, na)
    issued("pz-leaf", "pol-3", "/O=Neura Test/CN=pz.example.com", "pz_leaf", "pz-ca2", "pol-2", "63", nb, na)
    # Self-issued rollover: PZ CA1 (key pol-1) certifies PZ CA1 with key pol-4, which issues a leaf.
    issued("pz-rollover", "pol-4", "/O=Neura Test/CN=PZ CA1", "pz_ca2", "pz-ca1-zero", "pol-1", "64", nb, na)
    issued("pz-rollover-leaf", "pol-3", "/O=Neura Test/CN=pzr.example.com", "pz_leaf", "pz-rollover", "pol-4", "65", nb, na)
    # RSA: a PKCS#1 v1.5 root and an RSASSA-PSS-signed leaf.
    self_signed("rsa-root", "rsa-root", "/O=Neura Test/CN=RSA Root", "root", "20", "20240101000000Z",
                "20340101000000Z", ["-sha256"])
    if kept_pss is not None:
        (certs / "rsa-pss-leaf.der").write_bytes(kept_pss)
    else:
        issued("rsa-pss-leaf", "zoo-p256", "/O=Neura Test/CN=rsa.example.com", "rsa_leaf", "rsa-root", "rsa-root", "21",
               "20250101000000Z", "20300101000000Z",
               ["-sha256", "-sigopt", "rsa_padding_mode:pss", "-sigopt", "rsa_pss_saltlen:32", "-sigopt", "rsa_mgf1_md:sha256"])
    # A self-signed ML-DSA-44 certificate.
    self_signed("mldsa-root", "mldsa-root", "/O=Neura Test/CN=pq.example.com", "mldsa_root", "30",
                "20240101000000Z", "20340101000000Z", ["-sigopt", "deterministic:1"])
    # CSRs.
    run(["req", "-new", "-config", cnf, "-key", k("zoo-p256"), "-subj", "/C=US/O=Neura Test/CN=csr.example.com",
         "-reqexts", "csr_ext", "-sigopt", "nonce-type:1", "-sha256", "-outform", "DER", "-out", c("csr-p256")], out, env)
    (work / "attrs.cnf").write_text(ATTRS_CONFIG, encoding="utf-8")
    run(["req", "-new", "-config", str(work / "attrs.cnf"), "-key", k("zoo-ed25519"), "-outform", "DER", "-out",
         c("csr-ed25519")], out, env)
    # SubjectPublicKeyInfo and PKCS#8 (OneAsymmetricKey) of the zoo.
    for name in ZOO:
        run(["pkey", "-in", k(name), "-pubout", "-outform", "DER", "-out", str(certs / f"{name}.spki.der")], out)
        run(["pkcs8", "-topk8", "-nocrypt", "-in", k(name), "-outform", "DER", "-out", str(certs / f"{name}.p8.der")], out)
    # Encrypted PKCS#8 (PBES2). Randomized salt and IV: kept as the record.
    encrypted = out / "encrypted"
    encrypted.mkdir(exist_ok=True)
    for name, args in [("p256-aes256cbc-sha256", ["-v2", "aes-256-cbc", "-v2prf", "hmacWithSHA256", "-iter", "1000"]),
                       ("ed25519-aes128cbc-sha512", ["-v2", "aes-128-cbc", "-v2prf", "hmacWithSHA512", "-iter", "2048"]),
                       ("ed25519-aes192cbc-sha384", ["-v2", "aes-192-cbc", "-v2prf", "hmacWithSHA384", "-iter", "500"]),
                       ("p256-aes256cbc-sha1", ["-v2", "aes-256-cbc", "-v2prf", "hmacWithSHA1", "-iter", "1000"])]:
        path = encrypted / f"{name}.der"
        if not path.exists():
            key = "zoo-p256" if name.startswith("p256") else "zoo-ed25519"
            run(["pkcs8", "-topk8", "-in", k(key), "-outform", "DER", "-out", str(path), "-passout",
                 "pass:correct horse"] + args, out)
    record_ocsp(out, keys, certs, work, cnf, env)
    shutil.rmtree(work)
    print(out)
    return 0


def record_ocsp(out, keys, certs, work, cnf, env):
    """OCSP requests and responses from `openssl ocsp` acting as the responder for the intermediate. Responses carry
    the current time, so they are made once and kept (delete material/ocsp to record them again)."""
    ocsp = out / "ocsp"
    if ocsp.exists():
        return
    ocsp.mkdir()

    def pem(name):
        path = work / f"{name}.pem"
        run(["x509", "-inform", "DER", "-in", str(certs / f"{name}.der"), "-out", str(path)], out)
        return str(path)

    ecdsa = ["-sigopt", "nonce-type:1"]
    for name, section in [("ocsp-responder", "ocsp_responder"), ("ocsp-no-eku", "ocsp_no_eku")]:
        csr = str(work / f"{name}.csr")
        run(["req", "-new", "-config", cnf, "-key", str(keys / "ocsp.pem"), "-subj", f"/O=Neura Test/CN={name}", "-out",
             csr], out, env)
        run(["x509", "-req", "-in", csr, "-CA", str(certs / "inter.der"), "-CAform", "DER", "-CAkey",
             str(keys / "inter.pem"), "-extfile", cnf, "-extensions", section, "-set_serial", "500" if name == "ocsp-responder"
             else "501", "-not_before", "20250101000000Z", "-not_after", "20300101000000Z", "-outform", "DER", "-out",
             str(ocsp / f"{name}.der")] + ecdsa, out, env)
        run(["x509", "-inform", "DER", "-in", str(ocsp / f"{name}.der"), "-out", str(work / f"{name}.pem")], out)
    inter, leaf, revoked = pem("inter"), pem("leaf"), pem("neg-expired")
    unknown = pem("neg-wildcard")
    # The index: the leaf is valid, neg-expired (serial 107) is revoked for keyCompromise.
    (work / "index.txt").write_text(
        "V\t510101000000Z\t\t0102030405060708090A0B0C0D0E0F1011121314\tunknown\t/C=US/O=Neura Test/CN=www.example.com\n"
        "R\t210101000000Z\t250601120000Z,keyCompromise\t6B\tunknown\t/C=US/O=Neura Test/CN=old\n")
    # A digest option applies to the -cert options after it.
    requests = [("req-sha1-nonce", [], [leaf]), ("req-sha256", ["-sha256", "-no_nonce"], [leaf]),
                ("req-revoked", ["-sha256", "-no_nonce"], [revoked]), ("req-unknown", ["-no_nonce"], [unknown]),
                ("req-two", ["-sha256", "-no_nonce"], [leaf, revoked])]
    for name, extra, subjects in requests:
        args = ["ocsp", "-issuer", inter] + extra
        for cert in subjects:
            args += ["-cert", cert]
        run(args + ["-reqout", str(ocsp / f"{name}.der")], out)
    responses = [("resp-good-direct", "req-sha256", ["-rsigner", inter, "-rkey", str(keys / "inter.pem")]),
                 ("resp-good-nonce", "req-sha1-nonce", ["-rsigner", inter, "-rkey", str(keys / "inter.pem")]),
                 ("resp-revoked", "req-revoked", ["-rsigner", inter, "-rkey", str(keys / "inter.pem")]),
                 ("resp-unknown", "req-unknown", ["-rsigner", inter, "-rkey", str(keys / "inter.pem")]),
                 ("resp-two", "req-two", ["-rsigner", inter, "-rkey", str(keys / "inter.pem")]),
                 ("resp-delegated", "req-sha256", ["-rsigner", str(work / "ocsp-responder.pem"), "-rkey",
                                                   str(keys / "ocsp.pem")]),
                 ("resp-delegated-bykey", "req-sha256", ["-rsigner", str(work / "ocsp-responder.pem"), "-rkey",
                                                         str(keys / "ocsp.pem"), "-resp_key_id"]),
                 ("resp-no-eku", "req-sha256", ["-rsigner", str(work / "ocsp-no-eku.pem"), "-rkey", str(keys / "ocsp.pem")])]
    for name, request, extra in responses:
        run(["ocsp", "-index", str(work / "index.txt"), "-CA", inter, "-reqin", str(ocsp / f"{request}.der"), "-respout",
             str(ocsp / f"{name}.der"), "-ndays", "7"] + extra, out)


if __name__ == "__main__":
    sys.exit(main())
