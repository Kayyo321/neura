#!/usr/bin/env python3
"""Write tests/stdlib/character/exhaustive-tests.nra: every code point 0 through 0x10FFFF, every property.

    python3 tests/stdlib/character/generators/exhaustive.py --perl "PERL COMMAND"

PERL COMMAND is the perl whose Unicode::UCD is 17.0.0, as for
scripts/generate-unicode-tables.py; the pinned dump hash of that script is
checked. The expected values are computed here, per code point, from the
unpacked inversion maps of the dump and the Java definitions of
docs/lang/Stdlib.md section 8.5. They do not read the packed tables, so the
test checks the packing and the Neura lookups against the UCD data.

Three independent checks run before anything is written, and the script stops
when either finds a difference that is not listed below:

* Python's unicodedata (Unicode 15.1.0 in Python 3.13) for every code point
  whose Age is 15.1 or earlier: General_Category, Bidi_Class, Bidi_Mirrored,
  decimal and numeric values, str.isalpha, isnumeric, isalnum, isupper, and
  islower, and one-to-one str.upper, lower, and title results.
* Perl's regular expression engine, whose property tables are compiled into
  the perl executable, for every code point: \p{...} of the binary
  properties of REGEX_PROPERTIES, the categories L, Lt, Nd, and Cn included.
* The Unicode 17.0.0 case tables of stdlib/std/text/internal/unicode-case-data.nra,
  generated from the unmodified UCD files: every one-scalar Upper and Lower
  record equals the simple mapping, and every code point without a record
  maps to itself.

Each expected value is a digest per plane: digest = 31 * digest + value,
wrapping at 2^32, over the plane's code points in order (Std.Core.Hash.combine).
"""

import argparse
import importlib.util
import re
import shlex
import subprocess
import sys
import unicodedata
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("tables", ROOT / "scripts" / "generate-unicode-tables.py")
tables = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tables)

# Differences between Unicode 15.1.0 (Python) and 17.0.0 (the tables) for code points assigned by 15.1, each
# a property change in Unicode 16.0 or 17.0. The case changes also agree with the case data generated from the
# unmodified 17.0.0 UCD files (cross_check_case_data).
KNOWN_CHANGES = {
    # LATIN LETTER PHARYNGEAL VOICED FRICATIVE: Ll to Lo, so it is no longer a cased letter.
    ("category", 0x0295), ("isalpha", 0x0295), ("isalnum", 0x0295), ("islower", 0x0295),
    # AHOM CONSONANT SIGN MEDIAL RA: Mn and NSM to Mc and L.
    ("category", 0x1171E), ("bidirectional", 0x1171E),
    # New capitals U+A7DC, U+A7CB, U+A7D2, and U+A7D4 give these small letters uppercase mappings.
    *((name, p) for p in (0x019B, 0x0264, 0xA7D3, 0xA7D5) for name in ("upper", "title")),
    # NOT EQUIVALENT TO became Bidi_Mirrored.
    ("mirrored", 0x226D),
    # The mathematical nablas changed from L to ON.
    ("bidirectional", 0x1D6C1), ("bidirectional", 0x1D6FB), ("bidirectional", 0x1D735), ("bidirectional", 0x1D76F),
    ("bidirectional", 0x1D7A9),
    # The Unihan value of U+5146 changed from 10^6 to 10^12.
    ("numeric", 0x5146),
    # Cuneiform signs that gained numeric values.
    *((name, p) for p in (0x12038, 0x12039, 0x12079, 0x12226, 0x1222B, 0x1230B, 0x1230D, 0x12399)
      for name in ("numeric", "isnumeric")),
}


def per_code_point(data):
    """{property: function(code point) -> uint32 value} from the raw dump."""
    maps, lists = data["maps"], data["lists"]
    gc_ranges, bc_ranges = maps["gc"]["ranges"], maps["bc"]["ranges"]
    sc_ranges, blk_ranges = maps["sc"]["ranges"], maps["blk"]["ranges"]
    upper, lower, title = (tables.expand(data, name) for name in ("suc", "slc", "stc"))
    numeric = tables.expand(data, "nv")
    category_index = {code: i for i, (code, _) in enumerate(tables.CATEGORIES)}
    bidi_index = {code: i for i, (code, _) in enumerate(tables.BIDI_CLASSES)}
    script_index = {long: i for i, (long, _, _) in enumerate(tables.scripts(data))}
    block_index = {name: i + 1 for i, (name, _, _, _) in enumerate(tables.blocks(data))}
    block_index["No_Block"] = 0

    def gc(p):
        return tables.value_at(gc_ranges, p)

    def member(name):
        return lambda p: tables.in_list(lists[name], p)

    def letter(p):
        return gc(p) in ("Lu", "Ll", "Lt", "Lm", "Lo")

    def space_char(p):
        return gc(p) in ("Zs", "Zl", "Zp")

    def ignorable(p):
        return p <= 8 or 14 <= p <= 27 or 127 <= p <= 159 or gc(p) == "Cf"

    def latin(p):
        for first, last in ((65, 90), (97, 122), (0xFF21, 0xFF3A), (0xFF41, 0xFF5A)):
            if first <= p <= last:
                return p - first + 10
        return -1

    def numeric_value(p):
        if latin(p) >= 0:
            return latin(p)
        if p not in numeric:
            return -1
        value = Fraction(numeric[p])
        return int(value) if value.denominator == 1 and 0 <= value <= 2147483647 else -2

    def digit36(p):
        value = latin(p)
        if value < 0 and gc(p) == "Nd":
            value = int(numeric[p])
        return value if 0 <= value < 36 else -1

    def whitespace(p):
        if p in (0xA0, 0x2007, 0x202F):
            return False
        return space_char(p) or 9 <= p <= 13 or 28 <= p <= 31

    properties = {
        "get_type": lambda p: category_index[gc(p)],
        "bidi_class": lambda p: bidi_index[tables.value_at(bc_ranges, p)],
        "get_directionality": lambda p: 23 if gc(p) == "Cn" else bidi_index[tables.value_at(bc_ranges, p)],
        "is_upper": member("Uppercase"),
        "is_lower": member("Lowercase"),
        "is_title_case": lambda p: gc(p) == "Lt",
        "is_letter": letter,
        "is_digit": lambda p: gc(p) == "Nd",
        "is_letter_or_digit": lambda p: letter(p) or gc(p) == "Nd",
        "is_alphabetic": member("Alphabetic"),
        "is_alpha": letter,
        "is_numeric": lambda p: p in numeric,
        "is_alpha_numeric": lambda p: letter(p) or p in numeric,
        "is_whitespace": whitespace,
        "is_space_char": space_char,
        "is_space": member("White_Space"),
        "is_iso_control": lambda p: p <= 31 or 127 <= p <= 159,
        "is_defined": lambda p: gc(p) != "Cn",
        "is_ideographic": member("Ideographic"),
        "is_mirrored": member("Bidi_Mirrored"),
        "is_identifier_start": lambda p: p == 0x2E2F or tables.in_list(lists["ID_Start"], p),
        "is_identifier_part": lambda p: p == 0x2E2F or tables.in_list(lists["ID_Continue"], p) or ignorable(p),
        "is_identifier_ignorable": ignorable,
        "is_java_identifier_start": lambda p: letter(p) or gc(p) in ("Nl", "Sc", "Pc"),
        "is_java_identifier_part": lambda p: (letter(p) or gc(p) in ("Sc", "Pc", "Nd", "Nl", "Mc", "Mn") or
                                             ignorable(p)),
        "is_emoji": member("Emoji"),
        "is_emoji_presentation": member("Emoji_Presentation"),
        "is_emoji_modifier": member("Emoji_Modifier"),
        "is_emoji_modifier_base": member("Emoji_Modifier_Base"),
        "is_emoji_component": member("Emoji_Component"),
        "is_extended_pictographic": member("Extended_Pictographic"),
        "to_upper": lambda p: upper.get(p, p),
        "to_lower": lambda p: lower.get(p, p),
        "to_title": lambda p: title.get(p, p),
        "digit": digit36,
        "numeric_value": numeric_value,
        "script": lambda p: script_index[tables.value_at(sc_ranges, p)],
        "block": lambda p: block_index[tables.value_at(blk_ranges, p)],
    }
    return properties, numeric, upper, lower, title, gc


NEURA_EXPRESSIONS = {
    "get_type": "character.get_type(p) as uint32",
    "bidi_class": "character.bidi_class(p) as uint32",
    "get_directionality": "character.get_directionality(p) as uint32",
    "digit": "pattern(character.digit(p, 36))",
    "numeric_value": "pattern(character.numeric_value(p))",
    "to_upper": "character.to_upper(p)",
    "to_lower": "character.to_lower(p)",
    "to_title": "character.to_title(p)",
    "script": "unicode_script.of(p) as uint32",
    "block": "unicode_block.of(p) as uint32",
}


def neura_expression(name):
    return NEURA_EXPRESSIONS.get(name, f"bit(character.{name}(p))")


def cross_check_python(data, properties, numeric, upper, lower, title, gc):
    differences = []
    age = data["maps"]["age"]["ranges"]
    stable = lambda p: tables.value_at(age, p) not in ("Unassigned", "16.0", "17.0")

    def differ(name, p):
        if (name, p) not in KNOWN_CHANGES:
            differences.append((name, p))

    for p in range(0x110000):
        ch = chr(p)
        if stable(p):
            if unicodedata.category(ch) != gc(p):
                differ("category", p)
            bidi = unicodedata.bidirectional(ch)
            if bidi and bidi != tables.value_at(data["maps"]["bc"]["ranges"], p):
                differ("bidirectional", p)
            if bool(unicodedata.mirrored(ch)) != properties["is_mirrored"](p):
                differ("mirrored", p)
            python_numeric = unicodedata.numeric(ch, None)
            ours = Fraction(numeric[p]) if p in numeric else None
            if (python_numeric is None) != (ours is None) or (
                    ours is not None and abs(float(ours) - python_numeric) > 1e-9 * max(1.0, abs(python_numeric))):
                differ("numeric", p)
            decimal = unicodedata.decimal(ch, None)
            if (decimal is not None) != (gc(p) == "Nd") or (decimal is not None and decimal != int(numeric[p])):
                differ("decimal", p)
        if 0xD800 <= p <= 0xDFFF:
            continue
        if stable(p) or tables.value_at(age, p) == "Unassigned":
            for method, name in (("isalpha", "is_alpha"), ("isnumeric", "is_numeric"),
                                 ("isalnum", "is_alpha_numeric")):
                if getattr(ch, method)() != properties[name](p):
                    differ(method, p)
            if stable(p):
                for result, table, method in ((ch.upper(), upper, "upper"), (ch.lower(), lower, "lower"),
                                              (ch.title(), title, "title")):
                    if len(result) == 1 and ord(result) != table.get(p, p):
                        differ(method, p)
                if ch.isupper() != properties["is_upper"](p):
                    differ("isupper", p)
                if ch.islower() != properties["is_lower"](p):
                    differ("islower", p)
    return differences


# Binary properties that Perl's regular expression engine tests from the property tables compiled into the
# perl executable, a path separate from the Unicode::UCD files that the dump reads.
REGEX_PROPERTIES = (
    ("Alphabetic", "is_alphabetic"), ("Uppercase", "is_upper"), ("Lowercase", "is_lower"),
    ("Ideographic", "is_ideographic"), ("ID_Start", None), ("ID_Continue", None), ("Bidi_Mirrored", "is_mirrored"),
    ("White_Space", "is_space"), ("Emoji", "is_emoji"), ("Emoji_Presentation", "is_emoji_presentation"),
    ("Emoji_Modifier", "is_emoji_modifier"), ("Emoji_Modifier_Base", "is_emoji_modifier_base"),
    ("Emoji_Component", "is_emoji_component"), ("Extended_Pictographic", "is_extended_pictographic"),
    ("Lt", "is_title_case"), ("L", "is_letter"), ("Nd", "is_digit"), ("Cn", None),
)


def cross_check_perl_regex(perl, data, properties):
    """Compare each code point's \\p{...} results with the expected values: ID_Start and ID_Continue with the
    dump's lists, which is_identifier_start and is_identifier_part extend, and Cn with is_defined."""
    if perl is None:
        print("skipped the Perl regular expression check (no --perl)")
        return []
    tests = ", ".join(f"($c =~ /\\p{{{name}}}/ ? 1 : 0)" for name, _ in REGEX_PROPERTIES)
    program = ("no warnings; binmode STDOUT; for my $p (0 .. 0x10FFFF) { my $c = chr($p); "
               f"print join('', {tests}), \"\\n\"; }}")
    result = subprocess.run(shlex.split(perl) + ["-e", program], capture_output=True, check=True)
    lines = result.stdout.decode().split("\n")
    differences = []
    for p in range(0x110000):
        row = lines[p]
        for i, (name, ours) in enumerate(REGEX_PROPERTIES):
            got = row[i] == "1"
            if ours is None and name != "Cn":
                expected = tables.in_list(data["lists"][name], p)
            elif name == "Cn":
                expected = not properties["is_defined"](p)
            else:
                expected = bool(properties[ours](p))
            if got != expected:
                differences.append((f"perl \\p{{{name}}}", p))
    return differences


def cross_check_case_data(upper, lower):
    """Compare with the 17.0.0 case data generated from the unmodified UCD files."""
    source = (ROOT / "stdlib/std/text/internal/unicode-case-data.nra").read_text(encoding="utf-8")
    alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"

    def unpack(text):
        value = 0
        for c in text:
            value = value * 64 + alphabet.index(c)
        return value

    differences = []
    for name, width, simple in (("Upper", 16, upper), ("Lower", 12, lower)):
        table = re.search(rf'export const {name}: string = "([^"]*)"', source).group(1)
        records = {}
        for offset in range(0, len(table), width):
            values = [unpack(table[offset + i:offset + i + 4]) for i in range(0, width, 4)]
            records[values[0]] = [v for v in values[1:] if v != 0]
        for p in range(0x110000):
            mapped = records.get(p)
            if mapped is None:
                if simple.get(p, p) != p:
                    differences.append((name, p))
            elif len(mapped) == 1 and mapped[0] != simple.get(p, p):
                differences.append((name, p))
    return differences


def digests(properties):
    names = list(properties)
    result = {name: [] for name in names}
    functions = [properties[name] for name in names]
    for plane in range(17):
        values = [0] * len(names)
        for p in range(plane << 16, (plane + 1) << 16):
            for i, function in enumerate(functions):
                value = function(p)
                value = (1 if value else 0) if isinstance(value, bool) else value & 0xFFFFFFFF
                values[i] = (values[i] * 31 + value) & 0xFFFFFFFF
        for i, name in enumerate(names):
            result[name].append(values[i])
    return result


def write_test(expected):
    names = list(expected)
    lines = [
        "// test-options: --gpu=off --max-steps 20000000000",
        "// Generated by tests/stdlib/character/generators/exhaustive.py; do not edit. The GPU is off: under",
        "// gpu=auto the whole file ran for more than five minutes instead of about 20 seconds.",
        "let character = import:Std.Core.Character;",
        "let hashing = import:Std.Core.Hash;",
        "let test = import:Std.Test;",
        "let unicode_block = import:Std.Core.UnicodeBlock;",
        "let unicode_script = import:Std.Core.UnicodeScript;",
        "",
        "// Every code point of each plane (Stdlib.md section 8.5), every property: the digest of the plane's",
        "// values (digest = 31 * digest + value, wrapping, as Hash.combine) equals the digest of the values the",
        "// generator computed from the Unicode 17.0.0 data, one per property and plane. A bool counts as 1 or 0,",
        "// an enum as its ordinal, and an int as its two's complement pattern.",
        "let CharacterExhaustiveTests = module (",
        "    /// @brief 1 for true and 0 for false.",
        "    let bit = func(value: bool) -> uint32 => value ? 1 : 0;",
        "",
        "    /// @brief The 32-bit two's complement pattern of an int.",
        "    let pattern = func(value: int) -> uint32 =>",
        "        value < 0 ? ((value as int64) + 4294967296) as uint32 : value as uint32;",
        "",
        "    /// @brief Checks one plane against the expected digests, in the order of the properties.",
        "    let check_plane = func(plane: uint32, expected: uint32[]) -> void {",
        f"        last digests: uint32[] = [{', '.join('0' for _ in names)}];",
        "        for (let p: uint32 = plane * 65536; p < (plane + 1) * 65536; p += 1) {",
    ]
    for i, name in enumerate(names):
        lines.append(f"            digests[{i}] = hashing.combine(digests[{i}], {neura_expression(name)});")
    lines += [
        "        }",
        "        last names: string[] = [",
        *tables.wrap_items([f'"{name}"' for name in names], "            "),
        "        ];",
        f"        for (let i = 0; i < {len(names)}; i += 1) {{",
        "            test.assert_with(digests[i] == expected[i],",
        '                "plane " + plane.to_string() + ": " + names[i] + " differs from the Unicode 17.0.0 data");',
        "        }",
        "    };",
    ]
    for plane in range(17):
        values = [str(expected[name][plane]) for name in names]
        lines += [
            "",
            f"    export let test_plane_{plane} = func() -> void {{",
            f"        check_plane({plane}, [",
            *tables.wrap_items(values, "            "),
            "        ]);",
            "    };",
        ]
    lines += [");", ""]
    path = ROOT / "tests/stdlib/character/exhaustive-tests.nra"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {path.relative_to(ROOT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--perl", default="perl",
                        help="the perl whose Unicode::UCD is 17.0.0; 'none' skips the regular expression check")
    parser.add_argument("--dump", type=Path, help="read the dump from this file instead of running perl")
    args = parser.parse_args()
    dump = args.dump.read_bytes() if args.dump else tables.run_perl(args.perl)
    perl = args.perl if args.perl != "none" else None
    data = tables.parse_dump(dump)
    print(f"Python unicodedata {unicodedata.unidata_version}; tables {tables.VERSION}")
    properties, numeric, upper, lower, title, gc = per_code_point(data)
    differences = cross_check_python(data, properties, numeric, upper, lower, title, gc)
    differences += cross_check_perl_regex(perl, data, properties)
    differences += cross_check_case_data(upper, lower)
    if differences:
        for name, p in differences[:50]:
            print(f"difference: {name} U+{p:04X}", file=sys.stderr)
        print(f"{len(differences)} differences", file=sys.stderr)
        return 1
    print("cross-checks passed")
    write_test(digests(properties))
    return 0


if __name__ == "__main__":
    sys.exit(main())
