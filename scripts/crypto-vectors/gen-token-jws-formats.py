#!/usr/bin/env python3
"""Extract RFC 7515 A.6/A.7 and RFC 7797 JWS examples and verify with Python.

Generate independent HMAC rejection fixtures over their exact malformed protected
bytes. The source files and their SHA-256 pins are recorded under
/home/dev/neura-gate/030/crypto2/sources/token/jws-formats/SOURCES.txt.

    python3 scripts/crypto-vectors/gen-token-jws-formats.py [sources-dir]
"""
import hashlib
import hmac
import importlib
import json
import pathlib
import sys
import unicodedata

import rfc_text
from nra_vectors import write_module

ref = importlib.import_module("gen-token-jws")
RFC7797_SHA256 = "5bbed0e2df28126e147f79dde99058fd1ca18977b7f9f0e5e75626d8efa31138"


def compact_json(value) -> str:
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"))


def extract_json(section: list[str]) -> dict:
    start = next(index for index, line in enumerate(section) if line.strip() == "{")
    end = next(index for index in range(start + 1, len(section)) if section[index].strip() == "}")
    return ref.parse_wrapped_json("\n".join(section[start:end + 1]))


def check_signature(key, payload: str, signature: dict) -> None:
    protected = signature["protected"]
    algorithm = json.loads(ref.decode(protected))["alg"]
    signed = protected.encode("ascii") + b"." + payload.encode("ascii")
    ref.verify(key, algorithm, signed, ref.decode(signature["signature"]))


def make_hmac(key: dict, protected: bytes, payload: bytes, *, encoded: bool) -> dict:
    protected_segment = ref.encode(protected)
    payload_bytes = ref.encode(payload).encode("ascii") if encoded else payload
    signed = protected_segment.encode("ascii") + b"." + payload_bytes
    tag = hmac.digest(ref.decode(key["k"]), signed, "sha256")
    return {"protected": protected_segment, "signature": ref.encode(tag)}


def detached(signature: dict) -> str:
    return signature["protected"] + ".." + signature["signature"]


def read_formats(sources: pathlib.Path):
    rfc7797_path = sources / "jws-formats" / "rfc7797.txt"
    assert hashlib.sha256(rfc7797_path.read_bytes()).hexdigest() == RFC7797_SHA256
    text = rfc_text.lines(str(rfc7797_path))
    examples = rfc_text.section(text, "4.  Examples", "5.  Unencoded Payload Content Restrictions")
    key_start = next(i for i, line in enumerate(examples) if '"kty":"oct"' in line)
    key = ref.parse_jwk(examples[key_start - 1:])
    rows = []
    json_cases = []
    formats = [
        ("4.1.  Example with Header Parameters", "4.2.  Example with Header Parameters", True),
        ("4.2.  Example with Header Parameters", "5.  Unencoded Payload", False),
    ]
    for start, end, encoded in formats:
        scope = rfc_text.section(text, start, end)
        compact = ref.compact_after(scope, "display purposes only) is:")
        try:
            envelope = extract_json(scope)
        except StopIteration as exc:
            raise ValueError(f"RFC 7797 JSON example is missing: {start}") from exc
        protected = json.loads(ref.decode(envelope["protected"]))
        assert protected.get("b64", True) == encoded
        payload = ref.decode(envelope["payload"]) if encoded else envelope["payload"].encode("utf-8")
        verify_input = ref.encode(payload).encode("ascii") if encoded else payload
        signed = envelope["protected"].encode("ascii") + b"." + verify_input
        if not hmac.compare_digest(hmac.digest(ref.decode(key["k"]), signed, "sha256"),
                                   ref.decode(envelope["signature"])):
            raise ValueError("RFC 7797 HMAC differs")
        hdr, payload_segment, tag = compact.split(".")
        assert hdr == envelope["protected"] and tag == envelope["signature"]
        assert payload_segment == (ref.encode(payload) if encoded else "")
        missing = {name: value for name, value in envelope.items() if name != "payload"}
        rows.append([key_hex(key), payload.hex(), compact, detached(envelope), text_hex(compact_json(envelope)),
                     text_hex(compact_json(missing))])
        json_cases.append(envelope)

    docs = rfc_text.lines(str(sources / "rfc7515.txt"))
    general = extract_json(rfc_text.section(docs, "A.6.4.  Complete JWS", "A.7.  Example JWS"))
    flattened = extract_json(rfc_text.section(docs, "A.7.  Example JWS", "Appendix B."))
    compact_rows, _, _ = ref.read_7515(sources)
    rsa_key = json.loads(bytes.fromhex(compact_rows[1][2]))
    ec_key = json.loads(bytes.fromhex(compact_rows[2][2]))
    for candidate, trusted in zip(general["signatures"], (rsa_key, ec_key), strict=True):
        check_signature(trusted, general["payload"], candidate)
    check_signature(ec_key, flattened["payload"], flattened)
    examples7515 = [[key_hex(rsa_key), key_hex(ec_key), ref.decode(general["payload"]).hex(),
                     text_hex(compact_json(general)), text_hex(compact_json(flattened))]]

    bad_headers = [
        ('missing crit', b'{"alg":"HS256","b64":false}'),
        ('duplicate crit entry', b'{"alg":"HS256","b64":false,"crit":["b64","b64"]}'),
        ('unknown crit entry', b'{"alg":"HS256","b64":false,"crit":["b64","other"],"other":true}'),
        ('b64 string', b'{"alg":"HS256","b64":"false","crit":["b64"]}'),
        ('duplicate b64', b'{"alg":"HS256","b64":true,"b64":false,"crit":["b64"]}'),
        ('duplicate nested header', b'{"alg":"HS256","b64":false,"crit":["b64"],"other":{"x":0,"x":1}}'),
        ('wrong algorithm', b'{"alg":"HS384","b64":false,"crit":["b64"]}'),
    ]
    body = b"binary.\x00\xff"
    refused = [[name, detached(make_hmac(key, header, body, encoded=False))]
               for name, header in bad_headers]
    encoded_true = make_hmac(key, b'{"alg":"HS256","b64":true,"crit":["b64"]}', body, encoded=True)
    binary = make_hmac(key, b'{"alg":"HS256","b64":false,"crit":["b64"]}', body, encoded=False)

    second_key = {"kty": "oct", "k": ref.encode(bytes(range(32)))}
    other_binary = make_hmac(second_key, b'{"alg":"HS256","b64":false,"crit":["b64"]}', body, encoded=False)
    multiple = text_hex(compact_json({"signatures": [binary, other_binary]}))
    empty = make_hmac(key, b'{"alg":"HS256"}', b"", encoded=True)

    first, second = json_cases
    bad_serializations = [
        ("mixed signatures", compact_json({"payload": second["payload"], "signatures": [
            {k: v for k, v in case.items() if k != "payload"} for case in json_cases]})),
        ("unprotected algorithm", compact_json({**first, "header": {"alg": "HS256"}})),
        ("repeated nested member", compact_json({"payload": first["payload"], "signatures": [
            {k: v for k, v in first.items() if k != "payload"}]}).replace('"signature":', '"signature":"bad", "signature":', 1)),
        ("two envelope forms", compact_json({**first, "signatures": [first]})),
        ("repeated unprotected metadata", compact_json({**first, "header": {"kid": "t"}}).replace('"kid":"t"', '"kid":"t","kid":"s"')),
    ]
    unassigned = "\u0378"
    assert unicodedata.category(unassigned) == "Cn"
    signed = make_hmac(key, ref.decode(second["protected"]), unassigned.encode("utf-8"), encoded=False)
    bad_serializations.append(("unassigned character", compact_json({**signed, "payload": unassigned})))

    write_module("crypto-token", "jws-format-vectors.nra", "JwsFormatVectors", [
        "python3 scripts/crypto-vectors/gen-token-jws-formats.py",
        "RFC 7515 A.6/A.7 and RFC 7797 4.1/4.2. Signatures checked with Python hmac and cryptography.",
        "Supplemental binary payloads and malformed protected headers use independent HMAC.",
    ], [
        ("rfc7797", ["JWK JSON hex", "payload hex", "published compact JWS", "detached compact JWS",
                     "flattened JSON hex", "detached JSON hex"], rows),
        ("rfc7515", ["RSA JWK JSON hex", "EC JWK JSON hex", "payload hex", "general JSON hex",
                     "flattened JSON hex"], examples7515),
        ("bad_headers", ["case", "signed detached compact"], refused),
        ("bad_serializations", ["case", "JWS JSON hex"],
         [[name, text_hex(token)] for name, token in bad_serializations]),
        ("binary_detached", ["JWK JSON hex", "binary payload hex", "b64=false compact",
                             "b64=true compact", "detached JSON hex"],
         [[key_hex(key), body.hex(), detached(binary), detached(encoded_true), text_hex(compact_json(binary))]]),
        ("trusted_indices", ["first JWK hex", "second JWK hex", "binary payload hex", "general detached JSON hex"],
         [[key_hex(key), key_hex(second_key), body.hex(), multiple]]),
        ("empty_payload", ["JWK hex", "attached empty JSON hex", "detached JSON hex", "null payload JSON hex"],
         [[key_hex(key), text_hex(compact_json({**empty, "payload": ""})), text_hex(compact_json(empty)),
           text_hex(compact_json({**empty, "payload": None}))]]),
    ])


def text_hex(text: str) -> str:
    return text.encode("utf-8").hex()


def key_hex(key: dict) -> str:
    return text_hex(compact_json(key))


if __name__ == "__main__":
    read_formats(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ref.DEFAULT)
