#!/usr/bin/env python3
"""Generate RFC 9496/9380 ristretto255 and SHA-512 XMD known answers.

Extracts RFC 9496 Appendix A.1-A.3 and RFC 9380 Appendix K.3. Independent
reference answers use hashlib and libsodium 1.0.18. RFC 9497 Appendix A.1
blinds supply an independent group-hash check.

    python3 scripts/crypto-vectors/gen-advanced-ristretto255.py [sources-dir]
"""

import ctypes
import ctypes.util
import hashlib
import itertools
import pathlib
import re
import sys

import rfc_text
from nra_vectors import write_module

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/advanced")


def normalized(text: str) -> str:
    return "".join(text.split())


def read_9496(source: pathlib.Path) -> tuple[list[list[str]], list[list[str]], list[list[str]]]:
    lines = rfc_text.lines(str(source / "rfc9496.txt"))
    a1 = "\n".join(rfc_text.section(lines, "A.1.  Multiples", "A.2.  Invalid"))
    multiples = [[index, normalized(first + second)] for index, first, second in
                 re.findall(r"^ +B\[\s*(\d+)\]: ([0-9a-f ]+)\n +([0-9a-f]+)$", a1, re.MULTILINE)]
    if [int(row[0]) for row in multiples] != list(range(16)):
        raise ValueError("unexpected RFC 9496 generator multiples")

    a2 = rfc_text.section(lines, "A.2.  Invalid", "A.3.  Group Elements")
    invalid, reason, digits = [], "", ""
    for line in a2:
        text = line.strip()
        if text.startswith("#"):
            reason = text[2:]
        elif re.fullmatch("[0-9a-f ]+", text):
            digits += normalized(text)
            if len(digits) == 64:
                invalid.append([reason, digits])
                digits = ""
    if digits or len(invalid) != 29:
        raise ValueError(f"unexpected RFC 9496 invalid encodings: {len(invalid)}")

    a3 = "\n".join(rfc_text.section(lines, "A.3.  Group Elements", "A.4.  Square Root"))
    derivations = []
    pending = []
    for kind, first, second in re.findall(r"^ +([IO]): ([0-9a-f ]+)\n +([0-9a-f ]+)$", a3, re.MULTILINE):
        value = normalized(first + second)
        if kind == "I":
            pending.append(value)
        else:
            derivations.extend([[item, value] for item in pending])
            pending.clear()
    if pending or len(derivations) != 11:
        raise ValueError("unexpected RFC 9496 uniform vectors")
    return multiples, invalid, derivations


def expand(message: bytes, dst: bytes, length: int) -> bytes:
    hash_fn = hashlib.sha512
    if len(dst) > 255:
        dst = hash_fn(b"H2C-OVERSIZE-DST-" + dst).digest()
    suffix = dst + bytes([len(dst)])
    initial = hash_fn(bytes(128) + message + length.to_bytes(2, "big") + b"\0" + suffix).digest()
    count = (length + 63) // 64
    blocks = []
    previous = bytes(64)
    for i in range(1, count + 1):
        chained = bytes(a ^ b for a, b in zip(initial, previous))
        previous = hash_fn(chained + bytes([i]) + suffix).digest()
        blocks.append(previous)
    return b"".join(blocks)[:length]


def read_9380(source: pathlib.Path) -> list[list[str]]:
    lines = rfc_text.lines(str(source / "rfc9380.txt"))
    k3 = "\n".join(rfc_text.section(lines, "K.3.  expand_message_xmd", "K.4.  expand_message_xof"))
    dst = re.search(r"^ +DST += (\S+)", k3, re.MULTILINE)[1].encode()
    rows = []
    for vector in re.split(r"^ +msg +=", k3, flags=re.MULTILINE)[1:]:
        message_text, after = vector.split("len_in_bytes", 1)
        message = normalized(message_text).encode()
        length = int(re.match(r"\s*= (0x[0-9a-f]+)", after)[1], 16)
        text = after.split("uniform_bytes = ", 1)[1].strip().splitlines()
        expected = bytes.fromhex("".join(itertools.takewhile(lambda s: re.fullmatch(r"\s*[0-9a-f]+", s), text)))
        if expected != expand(message, dst, length):
            raise ValueError("SHA-512 XMD input did not match RFC 9380")
        rows.append([message.hex(), dst.hex(), str(length), expected.hex()])
    if len(rows) != 10:
        raise ValueError("unexpected RFC 9380 SHA-512 XMD examples")
    return rows


class Sodium:
    def __init__(self):
        self.library = ctypes.CDLL(ctypes.util.find_library("sodium") or "libsodium.so.23")
        self.library.sodium_init()
        self.library.sodium_version_string.restype = ctypes.c_char_p
        self.version = self.library.sodium_version_string().decode()

    def invoke(self, operation: str, *args: bytes, returns_status: bool = True) -> bytes:
        name = operation.format(group="ristretto255")
        function = getattr(self.library, name)
        function.argtypes = [ctypes.c_void_p] + [ctypes.c_void_p] * len(args)
        function.restype = ctypes.c_int if returns_status else None
        out = ctypes.create_string_buffer(32)
        status = function(out, *args)
        if returns_status and status != 0:
            raise ValueError(f"{name} failed")
        return out.raw

    def group_hash(self, message: bytes, dst: bytes) -> bytes:
        return self.invoke("crypto_core_{group}_from_hash", expand(message, dst, 64))

    def scalar(self, operation: str, *args: bytes) -> bytes:
        return self.invoke("crypto_core_{group}_scalar_" + operation, *args, returns_status=operation == "invert")


def read_9497(source: pathlib.Path, sodium: Sodium) -> list[list[str]]:
    lines = rfc_text.lines(str(source / "rfc9497.txt"))
    oprf = "\n".join(rfc_text.section(lines, "A.1.1.1.  Test Vector", "A.1.2.  VOPRF Mode"))
    dst = b"HashToGroup-OPRFV1-\0-ristretto255-SHA512"
    rows = []
    for vector in re.split(r"^A\.1\.1\.\d+\.  Test Vector[^\n]+", oprf, flags=re.MULTILINE)[1:]:
        def read(name):
            match = re.search(r"^ +" + name + r" = ([0-9a-f]+)((?:\n +[0-9a-f]+)*)", vector, re.MULTILINE)
            return bytes.fromhex(normalized(match[1] + match[2]))
        message = read("Input")
        blind = read("Blind")
        blinded = read("BlindedElement")
        expected = sodium.invoke("crypto_scalarmult_{group}", sodium.scalar("invert", blind), blinded)
        if expected != sodium.group_hash(message, dst):
            raise ValueError("RFC 9497 blinded result disagrees with group hash")
        rows.append([message.hex(), dst.hex(), expected.hex()])
    if len(rows) != 2:
        raise ValueError("unexpected RFC 9497 ristretto255 examples")
    return rows


def main() -> None:
    sources = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    sodium = Sodium()
    multiples, invalid, uniform = read_9496(sources)
    for data, expected in uniform:
        if sodium.invoke("crypto_core_{group}_from_hash", bytes.fromhex(data)).hex() != expected:
            raise ValueError("libsodium disagrees with RFC 9496 map")
    expansion = read_9380(sources)
    hashes = read_9497(sources, sodium)
    for width in [255, 256, 1024]:
        dst = bytes(i % 256 for i in range(width))
        message = b"Long DST"
        hashes.append([message.hex(), dst.hex(), sodium.group_hash(message, dst).hex()])
        expansion.append([message.hex(), dst.hex(), "65", expand(message, dst, 65).hex()])
    length = 255 * 64
    max_dst = b"Neura-rfc9380-maximum-length"
    max_case = [b"abc".hex(), max_dst.hex(), str(length), hashlib.sha256(expand(b"abc", max_dst, length)).hexdigest()]

    order = 2**252 + 27742317777372353535851937790883648493
    encode_scalar = lambda s: s.to_bytes(32, "little")
    scalar_pairs = [(0, 1), (1, order - 1), (order - 1, order - 1), (2**251, 2**251 + 3)]
    arithmetic = []
    for a, b in scalar_pairs:
        left, right = encode_scalar(a), encode_scalar(b)
        values = [left, right, sodium.scalar("add", left, right), sodium.scalar("sub", left, right),
                  sodium.scalar("mul", left, right), sodium.scalar("invert", right)]
        arithmetic.append([value.hex() for value in values])
    reduction = [[data.hex(), sodium.scalar("reduce", data).hex()] for data in [bytes(64), bytes([255] * 64),
                 order.to_bytes(64, "little"), hashlib.sha512(b"Ristretto255 wide scalar test").digest()]]

    write_module("crypto-advanced", "ristretto255-vectors.nra", "Ristretto255Vectors", [
        "python3 scripts/crypto-vectors/gen-advanced-ristretto255.py",
        "RFC 9496 A.1-A.3 and RFC 9380 K.3. RFC 9497 A.1.1 blinded points recovered independently.",
        f"Additional scalar and long-DST answers from hashlib and libsodium {sodium.version}.",
    ], [
        ("multiples", ["scalar multiplication index", "encoding"], multiples),
        ("invalid", ["reason", "encoding"], invalid),
        ("uniform", ["64 uniform bytes", "point"], uniform),
        ("expansion", ["message", "DST", "output length", "XMD bytes"], expansion),
        ("max_expansion", ["message", "DST", "output length", "SHA256 digest of XMD bytes"], [max_case]),
        ("hash_to_group", ["message", "DST", "point"], hashes),
        ("scalar_arithmetic", ["a", "b", "a + b", "a - b", "a * b", "inverse of b"], arithmetic),
        ("scalar_reduction", ["64 input bytes", "reduced scalar"], reduction),
        ("order", ["noncanonical scalar equal to the order"], [[encode_scalar(order).hex()]]),
    ])


if __name__ == "__main__":
    main()
