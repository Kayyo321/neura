#!/usr/bin/env python3
"""CRLite filter-cascade vectors for Std.Crypto.Pki.CrLite.

Sources (recorded under /home/dev/neura-gate/030/crypto2/sources/pki, see SOURCES.txt):
- mozilla/filter-cascade (Python reference, commit b5469a2d0efaf150e3ce4c04b0d21b4ba2306cf7). This script runs
  its FilterCascade unchanged to build new cascades. Its two third-party imports are replaced by stand-ins
  written here: `bitarray` (a plain little-endian bit list with the four methods the reference uses) and
  `deprecated` (a no-op decorator); `mmh3` is a stand-in that refuses to hash, so only the reference's SHA-256
  cascades are built here.
- mozilla/rust-cascade test_data (commit f05b3da98675a20afe37f2c4231deb7343599341): filters that the reference
  made (make-sample-data.py, and the Rust builder for the SHA-256-counter one). Membership for the sets that
  make-sample-data.py defines is known by construction (included -> true, excluded -> false); for the other files
  the rust-cascade tests' own expectations (this, that -> true; other -> false) are used.

Keys of the CRLite cascade are SHA-256(issuer SubjectPublicKeyInfo DER) || serial number content bytes, over
the recorded material certificates and synthetic serials.

    python3 scripts/crypto-vectors/gen-pki-crlite.py [sources-dir]
"""

import hashlib
import io
import pathlib
import sys
import tempfile
import types

from cryptography import x509
from cryptography.hazmat.primitives import serialization

from nra_vectors import write_module

SOURCES = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/pki")


class _BitArray:
    """The subset of `bitarray.bitarray` that filtercascade uses, little-endian."""

    def __init__(self, size=0, endian="little"):
        assert endian == "little"
        self.bits = [False] * size

    def setall(self, value):
        self.bits = [bool(value)] * len(self.bits)

    def __setitem__(self, index, value):
        self.bits[index] = bool(value)

    def __getitem__(self, index):
        return self.bits[index]

    def __len__(self):
        return len(self.bits)

    def tobytes(self):
        out = bytearray((len(self.bits) + 7) // 8)
        for i, bit in enumerate(self.bits):
            if bit:
                out[i // 8] |= 1 << (i % 8)
        return bytes(out)

    def tofile(self, f):
        f.write(self.tobytes())

    def frombytes(self, data):
        for byte in data:
            for i in range(8):
                self.bits.append(bool((byte >> i) & 1))


def load_reference(sources: pathlib.Path):
    bitarray = types.ModuleType("bitarray")
    bitarray.bitarray = _BitArray
    deprecated = types.ModuleType("deprecated")
    deprecated.deprecated = lambda *a, **k: (lambda f: f)
    mmh3 = types.ModuleType("mmh3")

    def refuse(*a, **k):
        raise RuntimeError("MurmurHash3 cascades come from the recorded rust-cascade test data")

    mmh3.hash = refuse
    sys.modules.update({"bitarray": bitarray, "deprecated": deprecated, "mmh3": mmh3})
    sys.path.insert(0, str(sources / "filter-cascade"))
    import filtercascade
    return filtercascade


def serial_bytes(cert: x509.Certificate) -> bytes:
    n = cert.serial_number
    return n.to_bytes((n.bit_length() + 8) // 8, "big", signed=True)


def build(fc_module, include, exclude, salt):
    fc = fc_module.FilterCascade([], defaultHashAlg=fc_module.fileformats.HashAlgorithm.SHA256, salt=salt,
                                 growth_factor=1.0, min_filter_length=177)
    fc.initialize(include=include, exclude=exclude)
    fc.verify(include=include, exclude=exclude)
    out = io.BytesIO()
    fc.tofile(out)
    return out.getvalue()


def predictable_serial_gen(start, end):
    """make-sample-data.py's generator."""
    counter = start
    while counter < end:
        counter += 1
        yield hashlib.sha256(counter.to_bytes(4, byteorder="big")).hexdigest()


def v1_keys(sources: pathlib.Path):
    """The three keys of rust-cascade's cascade_v1_murmur_from_file_bytes_test, read from its source."""
    import re
    text = (sources / "rust-cascade" / "src" / "lib.rs").read_text()
    out = []
    for name, expected in [("key_for_revoked_cert_1", "1"), ("key_for_revoked_cert_2", "1"), ("key_for_valid_cert", "0")]:
        body = re.search(r"let " + name + r" = vec!\[(.*?)\];", text, re.S).group(1)
        key = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", body))
        out.append(key.hex() + "=" + expected)
    return out


def main() -> int:
    sources = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    fc_module = load_reference(sources)
    material = sources / "material" / "certs"
    certs = {n: x509.load_der_x509_certificate((material / f"{n}.der").read_bytes())
             for n in ["root", "inter", "leaf", "neg-expired", "neg-wildcard", "neg-codesign", "sub-ca"]}
    inter_spki = certs["inter"].public_key().public_bytes(serialization.Encoding.DER,
                                                         serialization.PublicFormat.SubjectPublicKeyInfo)
    issuer_hash = hashlib.sha256(inter_spki).digest()
    revoked_names = ["neg-expired", "neg-codesign"]
    valid_names = ["leaf", "neg-wildcard", "sub-ca"]
    revoked = [issuer_hash + serial_bytes(certs[n]) for n in revoked_names]
    revoked += [issuer_hash + hashlib.sha256(b"revoked %d" % i).digest()[:16] for i in range(60)]
    valid = [issuer_hash + serial_bytes(certs[n]) for n in valid_names]
    valid += [issuer_hash + hashlib.sha256(b"valid %d" % i).digest()[:16] for i in range(1500)]
    cascades = []
    for name, include, exclude, salt in [("crlite-salted", revoked, valid, b"crlite test salt"),
                                         ("crlite-unsalted", revoked, valid, None),
                                         ("crlite-inverted", valid, revoked, b"inv")]:
        data = build(fc_module, include, exclude, salt)
        expectations = [k.hex() + "=1" for k in include[:12]] + [k.hex() + "=0" for k in exclude[:12]]
        cascades.append([name, data.hex(), "|".join(expectations)])
    decisions = [[n, "revoked"] for n in revoked_names] + [[n, "valid"] for n in valid_names]
    # Recorded reference filters with their membership.
    test_data = sources / "rust-cascade" / "test_data"
    small = sorted(set(predictable_serial_gen(0, 500)))
    large = sorted(set(predictable_serial_gen(500, 10_000)))
    recorded = []
    for name, sets in [("test_v2_sha256l32_salt_mlbf", True), ("test_v2_sha256l32_mlbf", True),
                       ("test_v2_murmur_mlbf", True), ("test_v2_murmur_inverted_mlbf", "inverted"),
                       ("test_v2_sha256l32_inverted_mlbf", "inverted"), ("test_v2_sha256ctr_salt_mlbf", False),
                       ("test_v1_murmur_mlbf", False), ("test_v1_murmur_short_mlbf", False)]:
        data = (test_data / name).read_bytes()
        if name.startswith("test_v1"):
            # A real CRLite filter: the keys and expectations of cascade_v1_murmur_from_file_bytes_test; the short
            # file must not parse.
            expectations = v1_keys(sources) if name == "test_v1_murmur_mlbf" else []
        else:
            expectations = [b"this", b"that"], [b"other"]
            if sets is True:
                expectations = ([b"this", b"that"] + [s.encode() for s in small[::25]],
                                [b"other"] + [s.encode() for s in large[::400]])
            elif sets == "inverted":
                expectations = ([b"this", b"that"] + [s.encode() for s in large[::400]],
                                [b"other"] + [s.encode() for s in small[::25]])
            expectations = [k.hex() + "=1" for k in expectations[0]] + [k.hex() + "=0" for k in expectations[1]]
        recorded.append([name, data.hex(), "|".join(expectations) if expectations else "does not parse"])
    print(write_module(
        "crypto-pki", "crlite-vectors.nra", "CrliteVectors",
        ["python3 scripts/crypto-vectors/gen-pki-crlite.py: cascades built by mozilla/filter-cascade (commit b5469a2d)",
         "and rust-cascade test_data (commit f05b3da9) with membership from make-sample-data.py and the crate's tests.",
         "Keys are hex; '=1' included, '=0' excluded."],
        [("cascades", ["name", "filter bytes", "key=expected|..."], cascades),
         ("decisions", ["material certificate (issuer: inter)", "expected"], decisions),
         ("recorded", ["rust-cascade test_data file", "bytes", "key=expected|..."], recorded),
         ("issuer", ["inter DER"], [[(material / "inter.der").read_bytes().hex()]]),
         ("subjects", ["name", "DER"], [[n, (material / f"{n}.der").read_bytes().hex()] for n in revoked_names + valid_names])]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
