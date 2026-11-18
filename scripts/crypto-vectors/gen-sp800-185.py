#!/usr/bin/env python3
"""Known answers for cSHAKE, KMAC, KMACXOF, TupleHash and TupleHashXOF.

Copies every sample of NIST's SP 800-185 example files (Cryptographic
Standards and Guidelines, "Examples with Intermediate Values"):

    https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/<name>.pdf

for <name> in cSHAKE_samples, KMAC_samples, KMACXOF_samples,
TupleHash_samples and TupleHashXOF_samples. It downloads each PDF (or reads it
from --pdf-dir), converts it with `pdftotext -layout`, and parses the inputs
and the "Outval". KMAC rows are then extended with `openssl mac` (KMAC128 and
KMAC256 over longer keys, messages and outputs; the OpenSSL version is
recorded in the file). Writes tests/stdlib/crypto-hash/support/sp800185-vectors.nra
and tests/stdlib/crypto-mac/support/kmac-vectors.nra.

    python3 scripts/crypto-vectors/gen-sp800-185.py [--pdf-dir DIR]
"""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile
import urllib.request

from nra_vectors import pattern, write_module

BASE = "https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/"
NAMES = ["cSHAKE_samples", "KMAC_samples", "KMACXOF_samples", "TupleHash_samples", "TupleHashXOF_samples"]
HEX_LINE = re.compile(r"^\s*([0-9A-Fa-f]{2}(\s+[0-9A-Fa-f]{2})*)\s*$")


def text_of(name: str, pdf_dir: pathlib.Path) -> str:
    pdf = pdf_dir / f"{name}.pdf"
    if not pdf.exists():
        with urllib.request.urlopen(urllib.request.Request(BASE + name + ".pdf", headers={"User-Agent": "curl/8"}), timeout=60) as response:
            pdf.write_bytes(response.read())
    return subprocess.run(["pdftotext", "-layout", str(pdf), "-"], check=True,
                          capture_output=True, text=True).stdout


def hex_after(lines: list[str], index: int) -> bytes:
    """The hex bytes on the lines after lines[index], up to the first other line."""
    out = []
    for line in lines[index + 1:]:
        match = HEX_LINE.match(line)
        if not match:
            break
        out.append(match.group(1))
    return bytes.fromhex(" ".join(out))


def string_after(lines: list[str], index: int) -> str:
    text = lines[index + 1].strip()
    if text in ("(empty string)", '"(null)"', '""'):
        return ""
    if not (text.startswith('"') and text.endswith('"')):
        raise ValueError(f"unexpected string line: {text!r}")
    return text[1:-1]


def samples(text: str) -> list[dict]:
    result = []
    for chunk in re.split(r"\n\s*Sample #\d+\s*\n", text)[1:]:
        lines = chunk.split("\n")
        sample: dict = {"tuples": []}
        for i, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("Security Strength:"):
                sample["strength"] = int(re.search(r"(\d+)", stripped).group(1))
            elif stripped == "Key is":
                sample["key"] = hex_after(lines, i)
            elif stripped == "Data is":
                sample["data"] = hex_after(lines, i)
            elif re.fullmatch(r"Tuple \d+", stripped):
                sample["tuples"].append(hex_after(lines, i))
            elif stripped.startswith("Requested output length is"):
                sample["bits"] = int(re.search(r"(\d+)-bits", stripped).group(1))
            elif stripped == "N is":
                sample["n"] = string_after(lines, i)
            elif stripped == "S (as a character string) is":
                sample["s"] = string_after(lines, i)
            elif stripped == "Outval is" and "out" not in sample:
                sample["out"] = hex_after(lines, i)
        if len(sample["out"]) * 8 != sample["bits"]:
            raise ValueError(f"output length mismatch in {sample}")
        result.append(sample)
    return result


def openssl_kmac(bits: int, key: bytes, message: bytes, custom: str, out: int, xof: bool) -> bytes:
    with tempfile.NamedTemporaryFile() as data:
        data.write(message)
        data.flush()
        command = ["openssl", "mac", "-binary", "-macopt", "hexkey:" + key.hex(), "-macopt", f"size:{out}",
                   "-in", data.name]
        if custom:
            command += ["-macopt", "custom:" + custom]
        if xof:
            command += ["-macopt", "xof:1"]
        command.append(f"KMAC{bits}")
        return subprocess.run(command, check=True, capture_output=True).stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pdf-dir", type=pathlib.Path)
    args = parser.parse_args()
    pdf_dir = args.pdf_dir or pathlib.Path(tempfile.mkdtemp())
    texts = {name: text_of(name, pdf_dir) for name in NAMES}

    cshake = [[str(s["strength"]), s["data"].hex(), s["n"], s["s"], str(len(s["out"])), s["out"].hex()]
              for s in samples(texts["cSHAKE_samples"])]
    tuple_rows = []
    for name, xof in [("TupleHash_samples", "false"), ("TupleHashXOF_samples", "true")]:
        for s in samples(texts[name]):
            tuple_rows.append([str(s["strength"]), xof, s["s"], str(len(s["out"])), s["out"].hex(),
                               ":".join(t.hex() for t in s["tuples"])])
    kmac_rows = []
    for name, xof in [("KMAC_samples", "false"), ("KMACXOF_samples", "true")]:
        for s in samples(texts[name]):
            kmac_rows.append([str(s["strength"]), xof, s["key"].hex(), s["data"].hex(), s["s"],
                              str(len(s["out"])), s["out"].hex(), "NIST " + name])
    version = subprocess.run(["openssl", "version"], check=True, capture_output=True, text=True).stdout.strip()
    for bits in (128, 256):
        for key_length, message_length, custom, out, xof in [
                (16, 0, "", 32, False), (32, 200, "App", 64, False), (48, 168, "", 200, True),
                (137, 300, "Long customization string over one block of padding", 48, False)]:
            key = pattern(key_length, 13, 1)
            message = pattern(message_length)
            tag = openssl_kmac(bits, key, message, custom, out, xof)
            kmac_rows.append([str(bits), "true" if xof else "false", key.hex(), message.hex(), custom,
                              str(out), tag.hex(), version])

    sources = [f"python3 scripts/crypto-vectors/gen-sp800-185.py: NIST SP 800-185 samples from",
               f"{BASE}{{{','.join(NAMES)}}}.pdf"]
    print(write_module("crypto-hash", "sp800185-vectors.nra", "Sp800185Vectors", sources,
                       [("cshake", ["security bits", "data", "N", "S", "output length", "output"], cshake),
                        ("tuple_hash", ["security bits", "xof", "S", "output length", "output",
                                        "tuple items in hex joined by ':'"], tuple_rows)]))
    print(write_module("crypto-mac", "kmac-vectors.nra", "KmacVectors",
                       sources + [f"and `openssl mac` KMAC128/KMAC256 ({version}) over pattern keys and messages."],
                       [("kmac", ["security bits", "xof", "key", "data", "S", "output length", "output",
                                  "source"], kmac_rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
