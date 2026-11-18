#!/usr/bin/env python3
"""Generate exact compact JWS signing fixtures from RFCs and independent signing tools.

The RFC 7515/7520 keys, headers and payloads are public examples. RSA-PSS
uses an independent RFC 8017 encoding and integer exponentiation, with
independent OpenSSL verification. ECDSA uses OpenSSL's deterministic RFC
6979 mode. Pure Ed448 uses the RFC 8032 blank example seed.
"""

import argparse
import hashlib
import hmac
import json
import pathlib
import re
import runpy

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, ed448, padding, rsa, utils

import nra_vectors
import rfc_text

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources")
PINNED = {
    "token/rfc7515.txt": "ecbf84674118e62526511c1237e1deaa86aada86d25b247623a2a7e04ae62e40",
    "token/rfc7520.txt": "14798c3b2d72e35375d5fb86d280a1e8df6109a6ee6cbfc091a0c57a8cc076c6",
    "asymmetric/rfc8032.txt": "ed63657ff389301282b169b0abde9b5dd2c7e4d524fdfa5da6ff3094fc93c4c3",
    "token/jws-signing/rfc9864.txt": "0f9b6dd26b3285fd9fe7f7e0a82e684d06512dee642703d51c6f88a7b0edaa2b",
}
HASHES = {"256": hashes.SHA256, "384": hashes.SHA384, "512": hashes.SHA512}

existing = runpy.run_path(str(pathlib.Path(__file__).with_name("gen-token-jws.py")))
decode, encode = existing["decode"], existing["encode"]
parse_jwk, key_hex = existing["parse_jwk"], existing["key_hex"]


def digest(hash_name, data):
    return hashlib.new(hash_name, data).digest()


def octets(integer, width):
    return integer.to_bytes(width, "big")


def split_compact(token):
    header, payload, signature = token.split(".")
    return decode(header), decode(payload), decode(signature)


def header_for(algorithm):
    return json.dumps({"alg": algorithm}, separators=(",", ":")).encode("ascii")


def signing_input(header, payload):
    return (encode(header) + "." + encode(payload)).encode("ascii")


def compact(header, payload, signature):
    return signing_input(header, payload).decode("ascii") + "." + encode(signature)


def public_jwk(private):
    return {key: value for key, value in private.items()
            if key not in {"d", "p", "q", "dp", "dq", "qi", "oth"}}


def extract_7515(lines, number):
    section = rfc_text.section(lines, f"A.{number}.  Example", f"A.{number + 1}.  Example")
    start = next(i for i, line in enumerate(section) if '{"kty"' in line)
    return parse_jwk(section[start:], include_private=True)


def rsa_private(jwk):
    parts = {name: int.from_bytes(decode(jwk[name]), "big") for name in ("n", "e", "d", "p", "q", "dp", "dq", "qi")}
    public = rsa.RSAPublicNumbers(parts["e"], parts["n"])
    return rsa.RSAPrivateNumbers(parts["p"], parts["q"], parts["d"], parts["dp"], parts["dq"], parts["qi"], public).private_key()


def xor(left, right):
    return bytes(a ^ b for a, b in zip(left, right, strict=True))


def mgf1(hash_name, hash_length, seed, length):
    return b"".join(digest(hash_name, seed + octets(index, 4))
                    for index in range((length + hash_length - 1) // hash_length))[:length]


def pss_salt(key, signature, hash_alg):
    public = key.public_key().public_numbers()
    bits = public.n.bit_length() - 1
    em = octets(pow(int.from_bytes(signature, "big"), public.e, public.n), (bits + 7) // 8)
    width = hash_alg.digest_size
    if em[-1] != 0xBC:
        raise ValueError("RFC PSS trailer")
    masked, check = em[:-width - 1], em[-width - 1:-1]
    data = bytearray(xor(masked, mgf1(hash_alg.name, width, check, len(masked))))
    data[0] &= 255 >> (8 * len(em) - bits)
    if any(data[:-width - 1]) or data[-width - 1] != 1:
        raise ValueError("RFC PSS salt separator")
    return bytes(data[-width:])


def sign_pss(key, message, hash_alg, salt):
    private = key.private_numbers()
    n, d = private.public_numbers.n, private.d
    bits = n.bit_length() - 1
    width = hash_alg.digest_size
    length = (bits + 7) // 8
    hashed = digest(hash_alg.name, message)
    check = digest(hash_alg.name, bytes(8) + hashed + salt)
    data = bytes(length - 2 * width - 2) + b"\1" + salt
    masked = bytearray(xor(data, mgf1(hash_alg.name, width, check, len(data))))
    masked[0] &= 255 >> (8 * length - bits)
    em = bytes(masked) + check + b"\xbc"
    return octets(pow(int.from_bytes(em, "big"), d, n), (n.bit_length() + 7) // 8)


def rsa_case(name, key_id, key, algorithm, header, payload, published=None):
    message = signing_input(header, payload)
    hash_alg = HASHES[algorithm[2:]]()
    if algorithm.startswith("PS"):
        salt = pss_salt(key, published, hash_alg) if published else digest(hash_alg.name, name.encode("ascii"))
        signature = sign_pss(key, message, hash_alg, salt)
        scheme = padding.PSS(mgf=padding.MGF1(hash_alg), salt_length=hash_alg.digest_size)
    else:
        salt = b""
        scheme = padding.PKCS1v15()
        signature = key.sign(message, scheme, hash_alg)
    key.public_key().verify(signature, message, scheme, hash_alg)
    if published is not None and signature != published:
        raise ValueError(f"{name}: RFC RSA signature mismatch")
    return [name, key_id, algorithm, header.hex(), payload.hex(), salt.hex(), compact(header, payload, signature)]


def hmac_case(name, key, algorithm, header, payload, published=None):
    message = signing_input(header, payload)
    signature = hmac.digest(decode(key["k"]), message, HASHES[algorithm[2:]]().name)
    if published is not None and not hmac.compare_digest(signature, published):
        raise ValueError(f"{name}: RFC HMAC signature mismatch")
    return [name, algorithm, key_hex(key), header.hex(), payload.hex(), compact(header, payload, signature)]


def ec_case(name, algorithm, private, curve, header, payload):
    key = ec.derive_private_key(int.from_bytes(decode(private["d"]), "big"), curve)
    public = key.public_key().public_numbers()
    width = (curve.key_size + 7) // 8
    jwk = {"kty": "EC", "crv": f"P-{curve.key_size}", "x": encode(octets(public.x, width)), "y": encode(octets(public.y, width))}
    if "x" in private and (jwk["x"], jwk["y"]) != (private["x"], private["y"]):
        raise ValueError(f"{name}: RFC public point differs")
    hash_alg = HASHES[algorithm[2:]]()
    message = signing_input(header, payload)
    der = key.sign(message, ec.ECDSA(hash_alg, deterministic_signing=True))
    key.public_key().verify(der, message, ec.ECDSA(hash_alg))
    r, s = utils.decode_dss_signature(der)
    signature = octets(r, width) + octets(s, width)
    return [name, algorithm, decode(private["d"]).hex(), key_hex(jwk), header.hex(), payload.hex(), compact(header, payload, signature)]


def ed448_case(source, payload):
    text = source.read_text(encoding="utf-8")
    section = text.split("\n7.4.  Test Vectors for Ed448\n", 1)[1].split("\n   -----1 octet", 1)[0]

    def value(field):
        match = re.search(field + r":\n((?:   [0-9a-f]+\n)+)", section)
        return bytes.fromhex("".join(match[1].split()))

    seed = value("SECRET KEY")
    reference = value("PUBLIC KEY")
    key = ed448.Ed448PrivateKey.from_private_bytes(seed)
    public = key.public_key().public_bytes_raw()
    if public != reference or key.sign(b"") != value("SIGNATURE"):
        raise ValueError("RFC 8032 Ed448 blank case differs")
    jwk = {"kty": "OKP", "crv": "Ed448", "x": encode(public), "alg": "Ed448", "key_ops": ["verify"]}
    header = header_for("Ed448")
    message = signing_input(header, payload)
    signature = key.sign(message)
    key.public_key().verify(signature, message)
    return [seed.hex(), key_hex(jwk), payload.hex(), compact(header, payload, signature)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", nargs="?", default=DEFAULT, type=pathlib.Path)
    parser.add_argument("--out-root", type=pathlib.Path)
    args = parser.parse_args()
    if args.out_root:
        nra_vectors.ROOT = args.out_root
    for name, expected in PINNED.items():
        if hashlib.sha256((args.sources / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"unexpected RFC bytes: {name}")
    source = args.sources / "token"
    values_7515, _, _ = existing["read_7515"](source)
    values_7520 = existing["read_7520"](source)
    official = {row[0]: row[3] for row in values_7515 + values_7520}
    rfc7515 = rfc_text.lines(str(source / "rfc7515.txt"))
    rfc7520 = rfc_text.lines(str(source / "rfc7520.txt"))
    _, main_payload, _ = split_compact(official["RFC 7515 A.1"])

    hmac_rows = []
    for name, key in (
        ("RFC 7515 A.1", extract_7515(rfc7515, 1)),
        ("RFC 7520 4.4", parse_jwk(rfc_text.section(rfc7520, "3.5.  Symmetric Key", "3.6.  Symmetric Key"))),
    ):
        hmac_rows.append(hmac_case(name, key, "HS256", *split_compact(official[name])))
    for algorithm in ("HS384", "HS512"):
        key = {"kty": "oct", "k": encode(digest(HASHES[algorithm[2:]]().name, algorithm.encode("ascii")))}
        hmac_rows.append(hmac_case(algorithm + " default header", key, algorithm, header_for(algorithm), b""))

    rsa_jwks = [
        extract_7515(rfc7515, 2),
        parse_jwk(rfc_text.section(rfc7520, "3.4.  RSA Private Key", "3.5.  Symmetric Key"), include_private=True),
    ]
    rsa_keys = [rsa_private(jwk) for jwk in rsa_jwks]
    rsa_rows = []
    for name, key_id, algorithm in (("RFC 7515 A.2", 0, "RS256"), ("RFC 7520 4.1", 1, "RS256"), ("RFC 7520 4.2", 1, "PS384")):
        rsa_rows.append(rsa_case(name, str(key_id), rsa_keys[key_id], algorithm, *split_compact(official[name])))
    for algorithm in ("RS384", "RS512", "PS256", "PS512"):
        rsa_rows.append(rsa_case(algorithm + " default header", "0", rsa_keys[0], algorithm, header_for(algorithm), main_payload))

    ec_rows = []
    for index, algorithm, curve in ((3, "ES256", ec.SECP256R1()), (4, "ES512", ec.SECP521R1())):
        name = f"RFC 7515 A.{index}"
        protected, payload, _ = split_compact(official[name])
        ec_rows.append(ec_case(name + " RFC6979", algorithm, extract_7515(rfc7515, index), curve, protected, payload))
    scalar = hashlib.sha384(b"public Neura ES384 signing fixture").digest()
    ec_rows.append(ec_case("ES384 default header", "ES384", {"d": encode(scalar)}, ec.SECP384R1(), header_for("ES384"), b""))
    ec7520 = parse_jwk(rfc_text.section(rfc7520, "3.2.  EC Private Key", "3.3.  RSA Public Key"), include_private=True)
    protected, payload, _ = split_compact(official["RFC 7520 4.3"])
    ec_rows.append(ec_case("RFC 7520 4.3 RFC6979", "ES512", ec7520, ec.SECP521R1(), protected, payload))

    print(nra_vectors.write_module("crypto-token", "jws-signing-vectors.nra", "JwsSigningVectors", [
        "python3 scripts/crypto-vectors/gen-token-jws-signing.py",
        "RFC 7515 A.1 through A.4, RFC 7520 3 and 4.1 through 4.4, RFC 8032 7.4 and RFC 9864.",
        "OpenSSL/cryptography, Python HMAC and RFC 8017 arithmetic generate signing expected bytes.",
    ], [
        ("hmac_cases", ["source", "alg", "oct JWK hex", "header hex", "payload hex", "compact"], hmac_rows),
        ("rsa_keys", ["n", "e", "d", "p", "q", "dp", "dq", "qi", "public JWK hex"],
         [[*(decode(jwk[field]).hex() for field in ("n", "e", "d", "p", "q", "dp", "dq", "qi")), key_hex(public_jwk(jwk))] for jwk in rsa_jwks]),
        ("rsa_cases", ["source", "key index", "alg", "header hex", "payload hex", "salt", "compact"], rsa_rows),
        ("ec_cases", ["source", "alg", "private scalar", "public JWK hex", "header hex", "payload hex", "compact"], ec_rows),
        ("ed448_case", ["RFC 8032 seed", "public JWK hex", "payload hex", "compact"],
         [ed448_case(args.sources / "asymmetric/rfc8032.txt", main_payload)]),
    ]))


if __name__ == "__main__":
    main()
