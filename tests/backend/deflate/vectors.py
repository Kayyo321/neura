#!/usr/bin/env python3
"""DEFLATE, zlib and gzip vectors for Std.Encoding.Internal.Deflate, checked against Python's zlib.

tests/backend/deflate.cmake builds the harness program (main.nra) against a scratch standard library that
holds tests/backend/deflate/harness.nra, then runs this script. Every input is generated here from fixed seeds,
so a run is deterministic. The directions:

- Python compresses (levels 0 to 9, the fixed, Huffman-only and RLE strategies, raw/zlib/gzip, window bits 9
  to 15) and the module decompresses, with the exact length as its output limit.
- The module compresses and Python's zlib and gzip decompress.
- The RFC 7692 section 7.2.3 examples, context takeover and window limits through Inflater and Deflater.
- Malformed and truncated streams, wrong checksums and headers, and output limits, each with its expected error.
"""

import argparse
import gzip
import heapq
import pathlib
import random
import struct
import subprocess
import sys
import zlib

TAIL = b"\x00\x00\xff\xff"
# The harness runs every vector in one execution; the step count is deterministic for these inputs.
MaxSteps = 20_000_000_000


def framed(messages):
    return b"".join(struct.pack(">I", len(m)) + m for m in messages)


def unframed(data):
    out, at = [], 0
    while at < len(data):
        (length,) = struct.unpack(">I", data[at:at + 4])
        out.append(data[at + 4:at + 4 + length])
        at += 4 + length
    return out


class Bits:
    """An LSB-first bit writer for hand-made DEFLATE streams."""

    def __init__(self):
        self.value, self.count = 0, 0

    def put(self, value, count):
        self.value |= value << self.count
        self.count += count
        return self

    def huffman(self, code, length):
        # Huffman codes are sent most significant bit first.
        return self.put(int(format(code, f"0{length}b")[::-1], 2), length)

    def bytes(self):
        return self.value.to_bytes((self.count + 7) // 8, "little")


def fixed_literal(bits, symbol):
    if symbol < 144:
        return bits.huffman(0x30 + symbol, 8)
    if symbol < 256:
        return bits.huffman(0x190 + symbol - 144, 9)
    if symbol < 280:
        return bits.huffman(symbol - 256, 7)
    return bits.huffman(0xC0 + symbol - 280, 8)


def corpus():
    rng = random.Random(7692)
    sentence = b"The quick brown fox jumps over the lazy dog while the slow cat watches. "
    text = b"".join(sentence[: rng.randrange(20, len(sentence))] + str(i).encode() for i in range(120))
    mixed = bytearray()
    while len(mixed) < 40000:
        if mixed and rng.random() < 0.5:
            distance = rng.randrange(1, min(len(mixed), 32768) + 1)
            length = rng.randrange(3, 300)
            for _ in range(length):
                mixed.append(mixed[-distance])
        else:
            mixed.extend(rng.randbytes(rng.randrange(1, 200)))
    # Byte frequencies in a Fibonacci sequence: a heavily skewed literal distribution. The compressor's
    # matches change each block's frequencies, so this does not reach the 15-bit code limit; the code-lengths
    # cases below test the limit directly.
    weights, a, b = [], 1, 1
    for _ in range(22):
        weights.append(a)
        a, b = b, a + b
    skewed = bytearray()
    for symbol, weight in enumerate(weights):
        skewed.extend([symbol * 11] * weight)
    rng.shuffle(skewed)
    return {
        "skewed": bytes(skewed[:60000]),
        "empty": b"",
        "one": b"a",
        "hello": b"Hello",
        "alphabet": bytes(range(256)),
        "random": rng.randbytes(1000),
        "text": text,
        "period": bytes((i * 7) % 13 for i in range(3000)),
        "zeros": bytes(70000),
        "mixed": bytes(mixed),
    }


def python_compress(data, wbits, level=6, strategy=zlib.Z_DEFAULT_STRATEGY):
    compressor = zlib.compressobj(level, zlib.DEFLATED, wbits, 9, strategy)
    return compressor.compress(data) + compressor.flush()


class Run:
    def __init__(self, work):
        self.work = work
        self.lines = []
        self.checks = []
        # The block type of the first block of each raw stream the module compressed.
        self.block_types = set()

    def add(self, operation, data, arguments, check, name):
        index = len(self.lines)
        # Names relative to the work directory keep the manifest, and so the step count, independent of where
        # the build tree is.
        source = f"{index:04d}.in"
        target = self.work / f"{index:04d}.out"
        (self.work / source).write_bytes(data)
        self.lines.append(" ".join([operation, source, target.name, *map(str, arguments)]))
        self.checks.append((name, target, check))

    def tier0_identity(self, neura, image, stride):
        """Reruns every `stride`-th operation at --tier=0 and compares its output, or its error, byte for byte
        with the default tier's. Returns the number of differences."""
        lines = []
        pairs = []
        for index in range(0, len(self.lines), stride):
            fields = self.lines[index].split(" ")
            fields[2] = "tier0-" + fields[2]
            lines.append(" ".join(fields))
            pairs.append((self.checks[index][0], self.work / self.lines[index].split(" ")[2],
                          self.work / fields[2]))
        manifest = self.work / "manifest-tier0.txt"
        manifest.write_text("\n".join(lines) + "\n")
        result = subprocess.run([str(pathlib.Path(neura).resolve()), "run", "--tier=0", "--max-steps", str(MaxSteps),
                                 str(pathlib.Path(image).resolve()), "--", manifest.name],
                                capture_output=True, text=True, timeout=3600, cwd=self.work)
        if result.returncode != 0 or result.stdout.strip() != f"ran {len(lines)}":
            sys.exit(f"tier 0 harness failed ({result.returncode}): {result.stdout}{result.stderr}")
        differences = 0
        for name, default, tier0 in pairs:
            for suffix in ("", ".error"):
                a, b = pathlib.Path(str(default) + suffix), pathlib.Path(str(tier0) + suffix)
                if a.exists() != b.exists() or (a.exists() and a.read_bytes() != b.read_bytes()):
                    differences += 1
                    print(f"FAIL tier 0 differs from the default tier: {name}")
        print(f"{len(pairs) - differences} of {len(pairs)} operations identical at --tier=0")
        return differences

    def execute(self, neura, image):
        manifest = self.work / "manifest.txt"
        manifest.write_text("\n".join(self.lines) + "\n")
        result = subprocess.run([str(pathlib.Path(neura).resolve()), "run", "--max-steps", str(MaxSteps), "--stats",
                                 str(pathlib.Path(image).resolve()), "--", manifest.name],
                                capture_output=True, text=True, timeout=3600, cwd=self.work)
        print(result.stderr.strip())
        if result.returncode != 0 or result.stdout.strip() != f"ran {len(self.lines)}":
            sys.exit(f"harness failed ({result.returncode}): {result.stdout}{result.stderr}")
        failures = 0
        for name, target, check in self.checks:
            error_file = pathlib.Path(str(target) + ".error")
            output = target.read_bytes() if target.exists() else None
            error = error_file.read_text() if error_file.exists() else None
            problem = check(output, error)
            if problem:
                failures += 1
                print(f"FAIL {name}: {problem}")
        if self.block_types != {0, 1, 2}:
            failures += 1
            print(f"FAIL block types: the compressor's first blocks used only types {sorted(self.block_types)}")
        print(f"{len(self.checks) - failures} of {len(self.checks)} DEFLATE checks passed; "
              f"first-block types seen: {sorted(self.block_types)}")
        return failures


def expect_bytes(expected):
    def check(output, error):
        if error is not None:
            return f"unexpected error {error!r}"
        if output != expected:
            return f"got {len(output)} bytes {output[:32].hex()}..., expected {len(expected)} bytes {expected[:32].hex()}..."
        return None
    return check


def expect_error(kind, text):
    def check(output, error):
        if error is None:
            return f"expected {kind} error {text!r}, got {len(output)} bytes"
        if not error.startswith(kind + " ") or text not in error:
            return f"expected {kind} error {text!r}, got {error!r}"
        return None
    return check


def expect_failure_or(expected):
    """A truncated stream must fail, or decode to exactly the original bytes."""
    def check(output, error):
        if error is not None:
            return None if error.startswith("encoding ") else f"unexpected error {error!r}"
        return None if output == expected else "truncated stream decoded to different bytes"
    return check


def expect_python(decode, expected, extra=None):
    def check(output, error):
        if error is not None:
            return f"unexpected error {error!r}"
        try:
            decoded = decode(output)
        except Exception as failure:  # noqa: BLE001 - report any decoder failure
            return f"Python cannot decode it: {failure}"
        if decoded != expected:
            return "Python decoded different bytes"
        return extra(output) if extra else None
    return check


def decode_messages(payloads, wbits, reset):
    out, decompressor = [], zlib.decompressobj(wbits=wbits)
    for payload in payloads:
        if reset:
            decompressor = zlib.decompressobj(wbits=wbits)
        out.append(decompressor.decompress(payload + TAIL))
    return out


def python_messages(messages, wbits, reset):
    out, compressor = [], zlib.compressobj(6, zlib.DEFLATED, wbits)
    for message in messages:
        if reset:
            compressor = zlib.compressobj(6, zlib.DEFLATED, wbits)
        payload = compressor.compress(message) + compressor.flush(zlib.Z_SYNC_FLUSH)
        assert payload.endswith(TAIL)
        out.append(payload[:-4])
    return out


def add_python_to_module(run, data):
    configurations = [(level, zlib.Z_DEFAULT_STRATEGY) for level in (0, 1, 6, 9)]
    configurations += [(6, strategy) for strategy in (zlib.Z_FIXED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE)]
    for name, value in data.items():
        limit = len(value)
        large = len(value) > 10000
        for level, strategy in configurations[:2] if large else configurations:
            label = f"{name} level {level} strategy {strategy}"
            run.add("inflate-raw", python_compress(value, -15, level, strategy), [limit], expect_bytes(value),
                    f"python raw {label}")
            if not large or level == 1:
                run.add("inflate-zlib", python_compress(value, 15, level, strategy), [limit], expect_bytes(value),
                        f"python zlib {label}")
                run.add("inflate-gzip", python_compress(value, 31, level, strategy), [limit], expect_bytes(value),
                        f"python gzip {label}")
        for wbits in (9, 12, 15):
            run.add("inflate-zlib", python_compress(value, wbits), [limit], expect_bytes(value),
                    f"python zlib window {wbits} {name}")


def add_module_to_python(run, data):
    def zlib_header(output):
        return None if output[:2] == b"\x78\x9c" else f"zlib header {output[:2].hex()}"

    def gzip_header(output):
        return None if output[:10] == bytes.fromhex("1f8b08000000000000ff") else f"gzip header {output[:10].hex()}"

    def block_type(output):
        run.block_types.add((output[0] >> 1) & 3)
        return None

    for name, value in data.items():
        run.add("deflate-raw", value, [], expect_python(lambda o: zlib.decompress(o, -15), value, block_type),
                f"module raw {name}")
        run.add("deflate-zlib", value, [], expect_python(zlib.decompress, value, zlib_header), f"module zlib {name}")
        run.add("deflate-gzip", value, [], expect_python(gzip.decompress, value, gzip_header), f"module gzip {name}")
    rng = random.Random(1951)
    for index in range(24):
        size = rng.choice([1, 2, 3, 17, 258, 259, 1000, 5000, 20000])
        alphabet = rng.randrange(1, 257)
        value = bytes(rng.randrange(alphabet) for _ in range(size))
        run.add("deflate-raw", value, [], expect_python(lambda o: zlib.decompress(o, -15), value),
                f"module raw random {index}")
        run.add("inflate-raw", python_compress(value, -15, rng.randrange(10)), [len(value)], expect_bytes(value),
                f"python raw random {index}")


def add_rfc7692(run):
    hello = b"Hello"
    examples = {
        "7.2.3.1": [bytes.fromhex("f248cdc9c90700")],
        "7.2.3.2": [bytes.fromhex("f248cdc9c90700"), bytes.fromhex("f200110000")],
        "7.2.3.3": [bytes.fromhex("000500faff48656c6c6f00")],
        "7.2.3.4": [bytes.fromhex("f348cdc9c9070000")],
        "7.2.3.5": [bytes.fromhex("f24805000000ffffcac9c90700")],
    }
    for section, payloads in examples.items():
        expected = [hello] * len(payloads)
        assert decode_messages(payloads, -15, False) == expected, section
        run.add("stream-inflate", framed(payloads), [15, 1000, 0], expect_bytes(framed(expected)),
                f"RFC 7692 {section}")
    assert decode_messages([b"\x00"], -15, False) == [b""]
    run.add("stream-inflate", framed([b"\x00"]), [15, 1000, 0], expect_bytes(framed([b""])),
            "RFC 7692 7.2.3.6 empty message")
    run.add("stream-deflate", framed([hello]), [15, 0],
            expect_bytes(framed([bytes.fromhex("f248cdc9c90700") + TAIL])), "RFC 7692 7.2.3.1 compressed")

    def shared_window(output):
        payloads = [frame[:-4] for frame in unframed(output)]
        if any(not frame.endswith(TAIL) for frame in unframed(output)):
            return "a payload does not end with 00 00 FF FF"
        if decode_messages(payloads, -15, False) != [hello, hello]:
            return "Python decoded different messages"
        return None if len(payloads[1]) < len(payloads[0]) else "the second message did not use the window"
    run.add("stream-deflate", framed([hello, hello]), [15, 0], lambda o, e: e or shared_window(o),
            "RFC 7692 7.2.3.2 shared window")


def add_streams(run, data):
    rng = random.Random(1950)
    text = data["text"]
    messages = []
    for _ in range(12):
        start = rng.randrange(len(text) - 400)
        messages.append(text[start:start + rng.randrange(0, 400)])
    messages.append(b"")
    messages.append(data["mixed"][:9000])
    for wbits in (9, 10, 12, 15):
        for reset in (0, 1):
            payloads = python_messages(messages, -wbits, reset)
            run.add("stream-inflate", framed(payloads), [wbits, 20000, reset], expect_bytes(framed(messages)),
                    f"python stream window {wbits} reset {reset}")
    for wbits in (8, 9, 12, 15):
        for reset in (0, 1):
            def check(output, error, wbits=wbits, reset=reset):
                if error is not None:
                    return f"unexpected error {error!r}"
                frames = unframed(output)
                if any(not frame.endswith(TAIL) for frame in frames):
                    return "a payload does not end with 00 00 FF FF"
                decoded = decode_messages([frame[:-4] for frame in frames], -wbits, reset)
                return None if decoded == messages else "Python decoded different messages"
            run.add("stream-deflate", framed(messages), [wbits, reset], check, f"module stream window {wbits} reset {reset}")
    # Window limits: a match 300 bytes back is beyond a 256-byte window. zlib's compressor reaches at most
    # 2^wbits - 262 bytes back, so the stream is made with a 1 KiB window.
    far = rng.randbytes(300)
    stream = python_messages([far + far[:50]], -10, 0)
    assert len(stream[0]) < 340, "the stream must use the 300-byte match"
    run.add("stream-inflate", framed(stream), [9, 1000, 0], expect_bytes(framed([far + far[:50]])),
            "window 9 accepts distance 300")
    run.add("stream-inflate", framed(stream), [8, 1000, 0], expect_error("encoding", "distance too far back"),
            "window 8 rejects distance 300")
    # With reset, the second message cannot refer to the first.
    shared = python_messages([far, far], -15, 0)
    run.add("stream-inflate", framed(shared), [15, 1000, 1], expect_error("encoding", "distance too far back"),
            "reset forgets the window")
    run.add("stream-inflate", framed(shared), [15, 1000, 0], expect_bytes(framed([far, far])), "kept window")


def zlib_header(method):
    """A zlib header with the given CMF byte, no dictionary and a correct check."""
    flags = next(f for f in range(256) if (method * 256 + f) % 31 == 0 and not f & 32)
    return bytes([method, flags])


def add_malformed(run, data):
    value = data["text"]
    raw = python_compress(value, -15)
    zlib_stream = python_compress(value, 15)
    gzip_stream = python_compress(value, 31)
    for cut in (0, 1, 2, len(raw) // 3, len(raw) // 2, len(raw) - 2, len(raw) - 1):
        run.add("inflate-raw", raw[:cut], [len(value)], expect_failure_or(value), f"truncated raw at {cut}")
    for cut in (1, 2, 5, len(zlib_stream) - 4, len(zlib_stream) - 1):
        run.add("inflate-zlib", zlib_stream[:cut], [len(value)], expect_error("encoding", ""), f"truncated zlib at {cut}")
    for cut in (3, 9, 10, 20, len(gzip_stream) - 8, len(gzip_stream) - 1):
        run.add("inflate-gzip", gzip_stream[:cut], [len(value)], expect_error("encoding", ""), f"truncated gzip at {cut}")

    run.add("inflate-raw", bytes([0x07]), [10], expect_error("encoding", "invalid block type"), "block type 3")
    run.add("inflate-raw", bytes([0x01, 0x05, 0x00, 0xFA, 0xFE]) + b"Hello", [10],
            expect_error("encoding", "stored block length does not match its complement"), "stored length check")
    run.add("inflate-raw", bytes([0x01, 0x05, 0x00, 0xFA, 0xFF]) + b"Hel", [10],
            expect_error("encoding", "unexpected end of data"), "stored block cut short")
    run.add("inflate-raw", Bits().put(1, 1).put(1, 2).bytes(), [10], expect_error("encoding", "unexpected end of data"),
            "fixed block without its end")
    # A match before any output.
    bits = Bits().put(1, 1).put(1, 2)
    fixed_literal(bits, 257).huffman(0, 5)
    fixed_literal(bits, 256)
    run.add("inflate-raw", bits.bytes(), [10], expect_error("encoding", "distance too far back"), "distance at start")
    # Literal/length symbols 286 and 287 and distance symbols 30 and 31 do not exist.
    for symbol in (286, 287):
        bits = Bits().put(1, 1).put(1, 2)
        fixed_literal(bits, 65)
        fixed_literal(bits, symbol)
        run.add("inflate-raw", bits.bytes() + b"\x00", [10], expect_error("encoding", "invalid literal/length symbol"),
                f"fixed symbol {symbol}")
    for symbol in (30, 31):
        bits = Bits().put(1, 1).put(1, 2)
        fixed_literal(bits, 65)
        fixed_literal(bits, 257).huffman(symbol, 5)
        run.add("inflate-raw", bits.bytes() + bytes(4), [10], expect_error("encoding", "invalid Huffman code"),
                f"fixed distance symbol {symbol}")
    # Dynamic headers: an over-subscribed code-length code, too many symbols, a repeat with nothing before it,
    # and no end-of-block code.
    header = Bits().put(1, 1).put(2, 2).put(0, 5).put(0, 5).put(15, 4)
    for _ in range(19):
        header.put(1, 3)
    run.add("inflate-raw", header.bytes() + bytes(4), [10],
            expect_error("encoding", "incomplete or over-subscribed code-length code"), "over-subscribed code lengths")
    run.add("inflate-raw", Bits().put(1, 1).put(2, 2).put(30, 5).put(0, 5).put(0, 4).bytes() + bytes(4), [10],
            expect_error("encoding", "too many length or distance symbols"), "287 literal/length symbols")

    def dynamic(code_lengths, symbols):
        """A dynamic block whose code-length code gives symbols 16, 17, 18 and 0 to 1 two bits each."""
        bits = Bits().put(1, 1).put(2, 2).put(0, 5).put(0, 5).put(0, 4)
        order = [16, 17, 18, 0]
        for symbol in order:
            bits.put(code_lengths.get(symbol, 0), 3)
        codes = {0: 0, 16: 1, 17: 2, 18: 3}  # canonical: two-bit codes in symbol order
        for symbol, extra, extra_bits in symbols:
            bits.huffman(codes[symbol], 2)
            bits.put(extra, extra_bits)
        return bits.bytes() + bytes(8)
    lengths = {16: 2, 17: 2, 18: 2, 0: 2}
    run.add("inflate-raw", dynamic(lengths, [(16, 0, 2)]), [10],
            expect_error("encoding", "repeated length with no first length"), "repeat first")
    run.add("inflate-raw", dynamic(lengths, [(18, 127, 7), (18, 127, 7), (18, 127, 7)]), [10],
            expect_error("encoding", "too many code lengths"), "too many lengths")
    run.add("inflate-raw", dynamic(lengths, [(18, 127, 7), (18, 109, 7)]), [10],
            expect_error("encoding", "no end-of-block code"), "no end of block")
    run.add("inflate-raw", raw + b"x", [len(value)], expect_error("encoding", "data after end of stream"),
            "raw trailing data")

    run.add("inflate-zlib", bytes([0x78, 0x9D]) + zlib_stream[2:], [len(value)],
            expect_error("encoding", "invalid zlib header check"), "zlib header check")
    run.add("inflate-zlib", zlib_header(0x79) + zlib_stream[2:], [len(value)],
            expect_error("encoding", "unsupported compression method"), "zlib method")
    run.add("inflate-zlib", bytes([0x78, 0xBB]) + zlib_stream[2:], [len(value)],
            expect_error("encoding", "preset dictionary not supported"), "zlib preset dictionary")
    run.add("inflate-zlib", zlib_header(0x88) + zlib_stream[2:], [len(value)],
            expect_error("encoding", "invalid window size"), "zlib window 16")
    run.add("inflate-zlib", zlib_stream[:-1] + bytes([zlib_stream[-1] ^ 1]), [len(value)],
            expect_error("encoding", "incorrect Adler-32 checksum"), "zlib checksum")
    run.add("inflate-zlib", zlib_stream + b"\x00", [len(value)], expect_error("encoding", "data after end of stream"),
            "zlib trailing data")
    # A zlib header that declares a 512-byte window but whose data reaches 1000 bytes back.
    distant = random.Random(9).randbytes(1000)
    distant = distant + distant
    narrow = zlib_header(0x18) + python_compress(distant, 15)[2:]
    run.add("inflate-zlib", narrow, [len(distant)], expect_error("encoding", "distance too far back"),
            "zlib header window enforced")

    run.add("inflate-gzip", b"\x1f\x8c" + gzip_stream[2:], [len(value)], expect_error("encoding", "not a gzip member"),
            "gzip magic")
    run.add("inflate-gzip", gzip_stream[:3] + bytes([0x20]) + gzip_stream[4:], [len(value)],
            expect_error("encoding", "reserved gzip flags set"), "gzip reserved flag")
    run.add("inflate-gzip", gzip_stream[:-8] + bytes([gzip_stream[-8] ^ 1]) + gzip_stream[-7:], [len(value)],
            expect_error("encoding", "incorrect CRC-32"), "gzip CRC")
    run.add("inflate-gzip", gzip_stream[:-4] + bytes([gzip_stream[-4] ^ 1]) + gzip_stream[-3:], [len(value)],
            expect_error("encoding", "incorrect length"), "gzip size")
    run.add("inflate-gzip", gzip_stream + gzip_stream, [2 * len(value)],
            expect_error("encoding", "data after end of stream"), "gzip second member")
    # Every optional header field, with a correct and then a wrong header CRC.
    body = python_compress(value, -15)
    trailer = struct.pack("<II", zlib.crc32(value), len(value))
    head = bytes([0x1F, 0x8B, 8, 4 | 8 | 16 | 2]) + bytes(6) + struct.pack("<H", 3) + b"xyz" + b"name\x00" + b"note\x00"
    run.add("inflate-gzip", head + struct.pack("<H", zlib.crc32(head) & 0xFFFF) + body + trailer, [len(value)],
            expect_bytes(value), "gzip optional fields")
    run.add("inflate-gzip", head + struct.pack("<H", (zlib.crc32(head) ^ 1) & 0xFFFF) + body + trailer, [len(value)],
            expect_error("encoding", "incorrect gzip header CRC"), "gzip header CRC")
    assert gzip.decompress(head + struct.pack("<H", zlib.crc32(head) & 0xFFFF) + body + trailer) == value


def add_code_lengths(run):
    """The compressor's length-limited Huffman construction: complete, within the limit, and optimal when no
    limit applies."""
    def check(frequencies, max_bits):
        def verify(output, error):
            if error is not None:
                return f"unexpected error {error!r}"
            lengths = list(output)
            used = [s for s, f in enumerate(frequencies) if f]
            if len(lengths) != len(frequencies):
                return "wrong number of lengths"
            if any(lengths[s] == 0 for s in used) or max(lengths) > max_bits:
                return f"a used symbol has no code or a code is too long: {lengths}"
            if sum(2 ** (max_bits - n) for n in lengths if n) != 2 ** max_bits:
                return f"the code is not complete: {lengths}"
            weights = [frequencies[s] for s in used]
            if len(used) >= 2 and max(huffman_depths(weights)) <= max_bits:
                # Without the limit in play the lengths must cost exactly what a Huffman code costs.
                cost = sum(frequencies[s] * lengths[s] for s in used)
                best = sum(w * d for w, d in zip(weights, huffman_depths(weights)))
                if cost != best:
                    return f"cost {cost} is not the Huffman cost {best}"
            return None
        return verify

    fibonacci, a, b = [], 1, 1
    for _ in range(30):
        fibonacci.append(a)
        a, b = b, a + b
    rng = random.Random(1952)
    cases = [
        ([0] * 30, 15), ([5] + [0] * 29, 15), ([0, 0, 7] + [0] * 27, 15), ([3, 3], 15),
        (fibonacci[:20], 15), (fibonacci[:30], 15), (fibonacci[:19], 7), (fibonacci[:12], 7),
        ([1] * 286, 15), ([rng.randrange(0, 1000) for _ in range(286)], 15),
        ([rng.choice([0, 1, 1, 2, 50, 4000]) for _ in range(286)], 15),
        ([rng.randrange(0, 20) for _ in range(19)], 7),
    ]
    for index, (frequencies, max_bits) in enumerate(cases):
        run.add("code-lengths", b"".join(struct.pack(">I", f) for f in frequencies), [max_bits],
                check(frequencies, max_bits), f"code lengths {index}")


def huffman_depths(weights):
    heap = [(w, i, [i]) for i, w in enumerate(weights)]
    depths = [0] * len(weights)
    heapq.heapify(heap)
    counter = len(weights)
    while len(heap) > 1:
        w1, _, a = heapq.heappop(heap)
        w2, _, b = heapq.heappop(heap)
        for i in a + b:
            depths[i] += 1
        heapq.heappush(heap, (w1 + w2, counter, a + b))
        counter += 1
    return depths


def add_garbage(run, data):
    """Random bytes and bit-flipped streams: the decoder must return bytes within the limit or an Encoding
    error, never trap. A trap would end the harness run, which fails the whole test."""
    def bounded(limit):
        def check(output, error):
            if error is not None:
                return None if error.startswith(("encoding ", "custom encoding.deflate.output_limit")) else \
                    f"unexpected error {error!r}"
            return None if len(output) <= limit else f"{len(output)} bytes exceed the limit {limit}"
        return check
    rng = random.Random(1953)
    for index in range(40):
        garbage = rng.randbytes(rng.randrange(1, 300))
        for operation in ("inflate-raw", "inflate-zlib", "inflate-gzip"):
            run.add(operation, garbage, [5000], bounded(5000), f"garbage {index} {operation}")
    for index, name in enumerate(["text", "mixed", "random", "period"]):
        stream = bytearray(python_compress(data[name][:5000], -15, 6))
        for flip in range(10):
            changed = bytearray(stream)
            bit = rng.randrange(len(changed) * 8)
            changed[bit // 8] ^= 1 << (bit % 8)
            run.add("inflate-raw", bytes(changed), [8000], bounded(8000), f"bit flip {name} {flip}")


def add_limits(run, data):
    value = data["text"]
    for operation, stream in (("inflate-raw", python_compress(value, -15)), ("inflate-zlib", python_compress(value, 15)),
                              ("inflate-gzip", python_compress(value, 31))):
        run.add(operation, stream, [len(value)], expect_bytes(value), f"{operation} limit equal to the size")
        run.add(operation, stream, [len(value) - 1], expect_error("custom", "encoding.deflate.output_limit"),
                f"{operation} limit one byte short")
    stored = python_compress(value, -15, 0)
    run.add("inflate-raw", stored, [100], expect_error("custom", "encoding.deflate.output_limit"), "stored block limit")
    # 10 MiB of zeros compress to about 10 KiB; a 64 KiB limit stops the decoder early.
    bomb = python_compress(bytes(10 * 1024 * 1024), -15, 9)
    assert len(bomb) < 20000
    run.add("inflate-raw", bomb, [65536], expect_error("custom", "encoding.deflate.output_limit"), "expansion bomb")
    run.add("inflate-raw", python_compress(b"", -15), [0], expect_bytes(b""), "empty stream with limit 0")
    run.add("stream-inflate", framed(python_messages([value], -15, 0)), [15, 100, 0],
            expect_error("custom", "encoding.deflate.output_limit"), "stream limit")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--neura", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--work", required=True)
    args = parser.parse_args()
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    data = corpus()
    run = Run(work)
    add_python_to_module(run, data)
    add_module_to_python(run, data)
    add_rfc7692(run)
    add_streams(run, data)
    add_malformed(run, data)
    add_limits(run, data)
    add_code_lengths(run)
    add_garbage(run, data)
    failures = run.execute(args.neura, args.image)
    # Every 8th operation again at tier 0: the outputs and errors must not depend on the execution tier.
    failures += run.tier0_identity(args.neura, args.image, 8)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
