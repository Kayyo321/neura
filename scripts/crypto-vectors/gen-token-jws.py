#!/usr/bin/env python3
"""Extract compact JWS/JWK examples and sign rejection fixtures independently.

Sources: RFC 7515 A.1-A.5, RFC 7517 A.1/A.3, RFC 7520 sections 3 and
4.1-4.4, and RFC 8037 A.2-A.4. Rejection fixtures use Python hmac and the RFC 7515 A.1
payload/key. Every positive signature is checked with Python hmac or
cryptography before the fixtures are written.

    python3 scripts/crypto-vectors/gen-token-jws.py [sources-dir]
"""

import base64
import hashlib
import hmac
import itertools
import json
import pathlib
import re
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519, padding, rsa, utils

import rfc_text
from nra_vectors import write_module

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/token")


def decode(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def compact_after(block: list[str], marker: str) -> str:
    start = next(i for i, line in enumerate(block) if marker in line)
    encoded = (line.strip() for line in block[start + 1:])
    lines = itertools.dropwhile(lambda line: not line, encoded)
    compact = "".join(itertools.takewhile(lambda line: re.fullmatch(r"[A-Za-z0-9_.-]+", line), lines))
    if compact.count(".") != 2:
        raise ValueError(f"could not extract compact serialization after {marker!r}")
    return compact


def parse_jwk(block: list[str], *, include_private: bool = False) -> dict[str, str]:
    match = re.search(r"\{.*?\}", "\n".join(block), re.DOTALL)
    if match is None:
        raise ValueError("missing example JWK")
    key = parse_wrapped_json(match[0])
    if include_private:
        return key
    return {name: value for name, value in key.items()
            if name not in {"d", "p", "q", "dp", "dq", "qi", "oth"}}


def parse_wrapped_json(text: str):
    # RFC wraps base64url values across lines. Preserve whitespace in unwrapped values.
    unwrapped = re.sub(r'"[^"]*"', lambda m: re.sub(r"\n\s*", "", m[0]), text)
    return json.loads(unwrapped)


def key_hex(jwk: dict[str, str]) -> str:
    return json.dumps(jwk, separators=(",", ":")).encode("utf-8").hex()


def rsa_key(jwk: dict[str, str]) -> rsa.RSAPublicKey:
    return rsa.RSAPublicNumbers(int.from_bytes(decode(jwk["e"]), "big"),
                               int.from_bytes(decode(jwk["n"]), "big")).public_key()


def verify(jwk: dict[str, str], algorithm: str, message: bytes, signature: bytes) -> None:
    if algorithm == "HS256":
        if not hmac.compare_digest(hmac.digest(decode(jwk["k"]), message, "sha256"), signature):
            raise ValueError("RFC HMAC signature differs")
    elif algorithm == "RS256":
        rsa_key(jwk).verify(signature, message, padding.PKCS1v15(), hashes.SHA256())
    elif algorithm == "PS384":
        digest = hashes.SHA384()
        rsa_key(jwk).verify(signature, message, padding.PSS(mgf=padding.MGF1(digest), salt_length=digest.digest_size),
                            digest)
    elif algorithm in {"ES256", "ES512"}:
        curve, digest = (ec.SECP256R1(), hashes.SHA256()) if algorithm == "ES256" else (ec.SECP521R1(), hashes.SHA512())
        x, y = (int.from_bytes(decode(jwk[axis]), "big") for axis in ("x", "y"))
        public = ec.EllipticCurvePublicNumbers(x, y, curve).public_key()
        half = (curve.key_size + 7) // 8
        if len(signature) != 2 * half:
            raise ValueError("RFC ECDSA signature length differs")
        r = int.from_bytes(signature[:half], "big")
        s = int.from_bytes(signature[half:], "big")
        public.verify(utils.encode_dss_signature(r, s), message, ec.ECDSA(digest))
    elif algorithm == "EdDSA":
        ed25519.Ed25519PublicKey.from_public_bytes(decode(jwk["x"])).verify(signature, message)
    else:
        raise ValueError(f"unexpected vector algorithm {algorithm}")


def vector(name: str, algorithm: str, jwk: dict[str, str], compact: str) -> list[str]:
    head, payload, signature = compact.split(".")
    if json.loads(decode(head))["alg"] != algorithm:
        raise ValueError(f"algorithm mismatch in {name}")
    if algorithm == "none":
        if signature:
            raise ValueError(f"nonempty signature in {name}")
    else:
        verify(jwk, algorithm, (head + "." + payload).encode("ascii"), decode(signature))
    return [name, algorithm, key_hex(jwk), compact, decode(payload).hex()]


def read_7515(sources: pathlib.Path) -> tuple[list[list[str]], dict[str, str], dict[str, str]]:
    lines = rfc_text.lines(str(sources / "rfc7515.txt"))
    rows = []
    keys = []
    for index, algorithm in enumerate(["HS256", "RS256", "ES256", "ES512", "none"], 1):
        section = rfc_text.section(lines, f"A.{index}.  Example", f"A.{index + 1}.  Example")
        key = {}
        if algorithm != "none":
            key_start = next(i for i, line in enumerate(section) if '{"kty"' in line)
            key = parse_jwk(section[key_start:])
        keys.append(key)
        marker = "representation using the JWS Compact Serialization (with line breaks"
        last_index = max(i for i, line in enumerate(section) if marker in line)
        compact = compact_after(section[last_index:], "for display purposes only):")
        rows.append(vector(f"RFC 7515 A.{index}", algorithm, key, compact))
    return rows, keys[0], keys[1]


def read_7517(sources: pathlib.Path) -> list[list[str]]:
    lines = rfc_text.lines(str(sources / "rfc7517.txt"))
    rows = []
    for name, following in [("A.1.", "A.2."), ("A.3.", "Appendix B.")]:
        block = rfc_text.section(lines, name + "  Example", following)
        start = next(i for i, line in enumerate(block) if '{"keys":' in line)
        end = next(i for i in range(start + 1, len(block)) if block[i].strip() == "}")
        keys = parse_wrapped_json("\n".join(block[start:end + 1]))["keys"]
        for key in keys:
            rows.append([name, key["kty"], key_hex(key)])
    return rows


def read_7520(sources: pathlib.Path) -> list[list[str]]:
    lines = rfc_text.lines(str(sources / "rfc7520.txt"))
    rsa_public = parse_jwk(rfc_text.section(lines, "3.3.  RSA Public Key", "3.4.  RSA Private Key"))
    ec_public = parse_jwk(rfc_text.section(lines, "3.1.  EC Public Key", "3.2.  EC Private Key"))
    symmetric = parse_jwk(rfc_text.section(lines, "3.5.  Symmetric Key", "3.6.  Symmetric Key"))
    rows = []
    for index, (algorithm, key) in enumerate([("RS256", rsa_public), ("PS384", rsa_public), ("ES512", ec_public),
                                             ("HS256", symmetric)], 1):
        section = rfc_text.section(lines, f"4.{index}.  ", f"4.{index + 1}.  ")
        compact = compact_after(section, "The resulting JWS object using the JWS Compact Serialization:")
        rows.append(vector(f"RFC 7520 4.{index}", algorithm, key, compact))
    return rows


def read_8037(sources: pathlib.Path) -> tuple[list[str], list[str]]:
    lines = rfc_text.lines(str(sources / "rfc8037.txt"))
    private_section = rfc_text.section(lines, "A.1.  Ed25519 Private Key", "A.2.  Ed25519 Public Key")
    seed = decode(parse_jwk(private_section, include_private=True)["d"])
    key = parse_jwk(rfc_text.section(lines, "A.2.  Ed25519 Public Key", "A.3.  JWK Thumbprint"))
    thumbprint_section = rfc_text.section(lines, "A.3.  JWK Thumbprint", "A.4.  Ed25519 Signing")
    thumbprint = next(line.strip().rstrip(",") for line in thumbprint_section
                      if re.fullmatch(r"\s*[0-9a-f]{64},\s*", line))
    canonical = json.dumps(key, sort_keys=True, separators=(",", ":")).encode("utf-8")
    if hashlib.sha256(canonical).hexdigest() != thumbprint:
        raise ValueError("RFC 8037 thumbprint differs")
    section = rfc_text.section(lines, "A.4.  Ed25519 Signing", "A.5.  Ed25519 Validation")
    compact = compact_after(section, "signing input, a dot, and base64url encoding of the signature):")
    signing, signature = compact.rsplit(".", 1)
    if ed25519.Ed25519PrivateKey.from_private_bytes(seed).sign(signing.encode("ascii")) != decode(signature):
        raise ValueError("RFC 8037 Ed25519 signature differs")
    return vector("RFC 8037 A.2/A.4", "EdDSA", key, compact), [seed.hex(), thumbprint]


def signed_hmac_compact(key: bytes, header: str, payload: str) -> str:
    signing = encode(header.encode("utf-8")) + "." + payload
    tag = hmac.digest(key, signing.encode("ascii"), "sha256")
    return signing + "." + encode(tag)


def main() -> None:
    sources = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    rows, hmac_jwk, rsa_jwk = read_7515(sources)
    rows.extend(read_7520(sources))
    example_8037, extra_8037 = read_8037(sources)
    rows.append(example_8037)
    _, payload, _ = rows[0][3].split(".")
    key = decode(hmac_jwk["k"])
    headers = [
        ("unknown critical header", '{"alg":"HS256","crit":["example.invalid"],"example.invalid":true}'),
        ("missing critical header", '{"alg":"HS256","crit":["example.invalid"]}'),
        ("duplicate critical name", '{"alg":"HS256","crit":["ext","ext"],"ext":true}'),
        ("invalid critical header type", '{"alg":"HS256","crit":"ext","ext":true}'),
        ("empty critical list", '{"alg":"HS256","crit":[]}'),
        ("registered parameter as critical", '{"alg":"HS256","crit":["alg"]}'),
        ("duplicate alg", '{"alg":"none","alg":"HS256"}'),
        ("escaped duplicate alg", r'{"alg":"none","\u0061lg":"HS256"}'),
        ("duplicate noncritical parameter", '{"alg":"HS256","x":1,"x":2}'),
    ]
    negative = [[name, signed_hmac_compact(key, header, payload)] for name, header in headers]
    protected, _, encoded_tag = rows[0][3].split(".")
    tag = decode(encoded_tag)
    altered_tag = bytes([tag[0] ^ 1]) + tag[1:]
    negative.append(["altered signature", protected + "." + payload + "." + encode(altered_tag)])
    optional = signed_hmac_compact(key, '{"alg":"HS256","example.invalid":true}', payload)
    der = rsa_key(rsa_jwk).public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    confused = signed_hmac_compact(der, '{"alg":"HS256"}', payload)
    public_as_hmac = key_hex({"kty": "oct", "k": encode(der)})
    write_module("crypto-token", "jws-vectors.nra", "JwsVectors", [
        "python3 scripts/crypto-vectors/gen-token-jws.py",
        "RFC 7515 A.1–A.5, RFC 7517 JWKs A.1/A.3, RFC 7520 4.1–4.4 and RFC 8037 A.1–A.4.",
        "Positive signatures checked with Python hmac/cryptography; malformed headers independently MACed.",
    ], [
        ("examples", ["source", "algorithm", "JWK JSON hex", "compact JWS", "payload hex"], rows),
        ("ed25519", ["public RFC test seed", "SHA-256 JWK thumbprint"], [extra_8037]),
        ("jwk_examples", ["RFC 7517 section", "kty", "JWK JSON hex"], read_7517(sources)),
        ("negative", ["reason", "compact JWS"], negative),
        ("header_controls", ["valid optional header", "HS256 JWS using the RSA public DER as key",
                             "RSA public DER encoded as distinct octet JWK JSON hex"],
         [[optional, confused, public_as_hmac]]),
    ])


if __name__ == "__main__":
    main()
