"""Build Neura's pinned Unicode 17.0 casing tables.

Run from the checkout root with the unmodified UCD files in DIRECTORY
and the Unicode License V3 text at LICENSE. The library loads the output
as ordinary Neura source. Running Neura needs neither Python nor UCD files.

Data: https://www.unicode.org/Public/17.0.0/ucd/
License: https://www.unicode.org/license.txt
"""

import argparse
from hashlib import sha256
from pathlib import Path


VERSION = "17.0.0"
DIGITS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"
DATA_HASHES = {
    "UnicodeData.txt": "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c",
    "SpecialCasing.txt": "efc25faf19de21b92c1194c111c932e03d2a5eaf18194e33f1156e96de4c9588",
    "CaseFolding.txt": "ff8d8fefbf123574205085d6714c36149eb946d717a0c585c27f0f4ef58c4183",
    "DerivedCoreProperties.txt": "24c7fed1195c482faaefd5c1e7eb821c5ee1fb6de07ecdbaa64b56a99da22c08",
    "PropList.txt": "130dcddcaadaf071008bdfce1e7743e04fdfbc910886f017d9f9ac931d8c64dd",
}


def records(path):
    """Read the semicolon-separated fields before each UCD comment."""
    for line in path.read_text(encoding="utf-8").splitlines():
        data = line.split("#", 1)[0].strip()
        if data:
            yield [field.strip() for field in data.split(";")]


def codepoints(text):
    return tuple(int(value, 16) for value in text.split())


def pack(value):
    return "".join(DIGITS[(value >> shift) & 63] for shift in (18, 12, 6, 0))


def mappings(table, width):
    records = []
    for point, replacement in sorted(table.items()):
        if replacement != (point,):
            assert 1 <= len(replacement) <= width
            records.append(pack(point))
            records.extend(pack(value) for value in replacement)
            records.append(pack(0) * (width - len(replacement)))
    return "".join(records)


def ranges(points):
    """Pack membership transitions. An odd number of transitions means inside."""
    boundaries = []
    for point in sorted(points):
        if boundaries and point == boundaries[-1]:
            boundaries[-1] = point + 1
        else:
            boundaries.extend((point, point + 1))
    return "".join(pack(boundary) for boundary in boundaries)


def properties(path, wanted):
    points = {name: set() for name in wanted}
    for field, name, *_ in records(path):
        if name not in points:
            continue
        ends = field.split("..")
        first, last = int(ends[0], 16), int(ends[-1], 16)
        points[name].update(range(first, last + 1))
    return points


def generate(ucd, license_file):
    for name, expected in DATA_HASHES.items():
        if sha256((ucd / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"{name} must be the unmodified UCD {VERSION} file")

    simple_lower = {}
    simple_upper = {}
    combining = set()
    above = set()
    for record in records(ucd / "UnicodeData.txt"):
        point = int(record[0], 16)
        if record[3] != "0":
            combining.add(point)
        if record[3] == "230":
            above.add(point)
        if record[12]:
            simple_upper[point] = int(record[12], 16)
        if record[13]:
            simple_lower[point] = int(record[13], 16)
    lower = {point: (mapped,) for point, mapped in simple_lower.items()}
    upper = {point: (mapped,) for point, mapped in simple_upper.items()}
    for point, low, _, high, condition, *_ in records(ucd / "SpecialCasing.txt"):
        if not condition:
            lower[int(point, 16)] = codepoints(low)
            upper[int(point, 16)] = codepoints(high)
    fold = {
        int(point, 16): codepoints(mapped)
        for point, status, mapped, *_ in records(ucd / "CaseFolding.txt")
        if status in ("C", "F")
    }
    simple_fold = {}
    for point in simple_lower.keys() | simple_upper.keys():
        capital = simple_upper.get(point, point)
        simple_fold[point] = (simple_lower.get(capital, capital),)

    core = properties(ucd / "DerivedCoreProperties.txt", ("Cased", "Case_Ignorable"))
    dotted = properties(ucd / "PropList.txt", ("Soft_Dotted",))
    data = {
        "Lower": mappings(lower, 2),
        "Upper": mappings(upper, 3),
        "Fold": mappings(fold, 3),
        "SimpleFold": mappings(simple_fold, 1),
        "Cased": ranges(core["Cased"]),
        "CaseIgnorable": ranges(core["Case_Ignorable"]),
        "SoftDotted": ranges(dotted["Soft_Dotted"]),
        "Combining": ranges(combining),
        "Above": ranges(above),
    }

    notice = license_file.read_text(encoding="utf-8").splitlines()
    if not notice or notice[0] != "UNICODE LICENSE V3":
        raise ValueError("expected Unicode License V3")
    lines = [
        f"// Generated from the Unicode Character Database {VERSION}.",
        "// Regenerate with tests/stdlib/string/generators/unicode-case.py.",
        "// Each scalar or property boundary occupies four printable ASCII bytes.",
        "// Mapping records hold a key and its values, padded with zero.",
        "//",
        *("// " + line if line else "//" for line in notice),
        "",
        "/// @brief Compact Unicode case mappings and property ranges for deterministic text conversion.",
        "export let UnicodeCaseData = module (",
    ]
    descriptions = {
        "Lower": "Lowercase mappings indexed by scalar, including expansions.",
        "Upper": "Uppercase mappings indexed by scalar, including expansions.",
        "Fold": "Full locale-independent case-fold mappings, including expansions.",
        "SimpleFold": "Single-scalar mappings used for simple case-insensitive comparisons.",
        "Cased": "Ranges of scalars with the Unicode Cased property.",
        "CaseIgnorable": "Ranges of scalars ignored when testing neighboring case context.",
        "SoftDotted": "Ranges of soft-dotted scalars used by contextual casing rules.",
        "Combining": "Ranges of scalars with a nonzero canonical combining class.",
        "Above": "Ranges of scalars with canonical combining class 230 (Above).",
    }
    for name, value in data.items():
        lines.append(f"    /// @brief {descriptions[name]}")
        lines.append(f'    export const {name}: string = "{value}";')
    lines.append(");")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("license", type=Path)
    parser.add_argument(
        "--output", type=Path, default=Path("stdlib/std/text/internal/unicode-case-data.nra")
    )
    args = parser.parse_args()
    args.output.write_text(generate(args.directory, args.license), encoding="utf-8")
