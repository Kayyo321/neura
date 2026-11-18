#!/usr/bin/env python3
"""Write tests/stdlib/character/names-tests.nra: the names of every code point, both ways.

    python3 tests/stdlib/character/generators/names.py --perl "PERL COMMAND" --ucd DIRECTORY \\
        [--icu "UCONV COMMAND"] [--java-names FILE]

PERL COMMAND and DIRECTORY are those of scripts/generate-unicode-tables.py,
whose pinned dump hash and UCD file hashes are checked. The expected names are
computed here, per code point, straight from the unpacked UCD files and the
Java rule of docs/lang/Stdlib.md section 8.5; they do not read the packed
tables, so the test checks the packing and the Neura lookups against the data.

Independent checks run before anything is written, and the script stops when
one finds a difference that is not listed below:

* Perl's Unicode::UCD 17.0.0 Name property (prop_invmap and, for Hangul
  syllables, charnames::viacode) for every code point.
* Python's unicodedata.name (Unicode 15.1.0) for every code point whose Age is
  15.1 or earlier.
* ICU's Any-Name transliterator (uconv, ICU 77, Unicode 16.0) for every code
  point whose Age is 16.0 or earlier, when --icu is given.
* Java's Character.getName, from the output of JavaNames.java, when
  --java-names is given (OpenJDK 17.0.21, Unicode 13.0, was used): every
  name Java gives equals the expected get_name, and every code point Java does
  not name either has no expected name or was assigned after Unicode 13.0.

Each expected value is a digest per plane: for each code point in order, the
bytes of the name and then 256, or 257 when there is no name, combined as
digest = 31 * digest + value, wrapping at 2^32 (Std.Core.Hash.combine).
"""

import argparse
import importlib.util
import re
import shlex
import subprocess
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("tables", ROOT / "scripts" / "generate-unicode-tables.py")
tables = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tables)

# Perl 5.44's Unicode::UCD 17.0.0 names U+18D00 through U+18D1E "TANGUT IDEOGRAPH SUPPLEMENT-<hex>", from the
# label of their UnicodeData.txt range; DerivedName.txt and ICU name them "TANGUT IDEOGRAPH-<hex>".
PERL_DIFFERENCES = set(range(0x18D00, 0x18D1F))

# CPython's unicodedata generates the names of the CJK unified ideographs and Hangul syllables only, so it has no
# name for the other patterned ranges (NR2) that 15.1 assigned.
PYTHON_UNNAMED = (range(0x17000, 0x18800), range(0x18D00, 0x18D1F))

PERL_PROGRAM = r"""
use strict;
use warnings;
use charnames ();
use Unicode::UCD qw(prop_invmap);
binmode STDOUT, ':raw';
my ($list, $map) = prop_invmap('Name');
for my $i (0 .. $#$list) {
    my $value = $map->[$i];
    next if ref $value || $value eq '';
    my $end = $i < $#$list ? $list->[$i + 1] : 0x110000;
    for my $point ($list->[$i] .. $end - 1) {
        my $name = $value;
        if ($name eq '<hangul syllable>') {
            $name = charnames::viacode($point);
        } elsif ($name =~ /<code point>/) {
            my $hex = sprintf('%04X', $point);
            $name =~ s/<code point>/$hex/;
        }
        printf "%X\t%s\n", $point, $name;
    }
}
"""


def expected_names(directory, data):
    """(unicode_name, java_name): dicts from code point to name, from the pinned UCD files alone."""
    paths = tables.read_name_files(directory)
    unicode_names = {}
    for cps, name in tables.ucd_records(paths["DerivedName.txt"]):
        low, _, high = cps.partition("..")
        for point in range(int(low, 16), int(high or low, 16) + 1):
            unicode_names[point] = name.replace("*", f"{point:04X}")
    blk = data["maps"]["blk"]["ranges"]
    java = {}
    first = None
    for fields in tables.ucd_records(paths["UnicodeData.txt"]):
        point, name, old_name = int(fields[0], 16), fields[1], fields[10]
        if name.endswith(", First>"):
            first = point
            continue
        points = range(first, point + 1) if name.endswith(", Last>") else (point,)
        for p in points:
            if name == "<control>":
                if p in tables.JAVA_CONTROL_NAMES:
                    java[p] = tables.JAVA_CONTROL_NAMES[p][0]
                elif old_name:
                    java[p] = old_name
            elif not name.startswith("<"):
                java[p] = name
            if p not in java:
                block = re.sub(r"[ _\-]", " ", tables.value_at(blk, p).upper())
                java[p] = f"{block} {p:X}"
    return unicode_names, java


def run(command, program=None, text=None):
    result = subprocess.run(shlex.split(command) + (["-"] if program else []),
                            input=(program or text).encode(), capture_output=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"{command} failed: {result.stderr.decode(errors='replace')}")
    return result.stdout.decode("utf-8")


def cross_check(data, unicode_names, java, perl, icu, java_names):
    differences = []
    age = data["maps"]["age"]["ranges"]
    versions = {"Unassigned": (99, 0)}

    def age_of(p):
        value = tables.value_at(age, p)
        if value not in versions:
            versions[value] = tuple(int(part) for part in value.split("."))
        return versions[value]

    # Perl.
    perl_names = {}
    for line in run(perl, PERL_PROGRAM).splitlines():
        point, name = line.split("\t")
        perl_names[int(point, 16)] = name
    for p in sorted(perl_names.keys() | unicode_names.keys()):
        if perl_names.get(p) != unicode_names.get(p) and p not in PERL_DIFFERENCES:
            differences.append(("perl", p, perl_names.get(p), unicode_names.get(p)))
    print(f"perl: {len(perl_names)} names")

    # Python.
    checked = 0
    for p in range(0x110000):
        if age_of(p) > (15, 1) or 0xD800 <= p <= 0xDFFF:
            continue
        name = unicodedata.name(chr(p), None)
        expected = unicode_names.get(p)
        if name is None and any(p in r for r in PYTHON_UNNAMED):
            continue
        checked += 1
        if name != expected:
            differences.append(("python", p, name, expected))
    print(f"python {unicodedata.unidata_version}: {checked} code points")

    # ICU.
    if icu:
        points = [p for p in range(0x110000) if age_of(p) <= (16, 0) and not 0xD800 <= p <= 0xDFFF]
        text = "".join(chr(p) for p in points)
        output = run(icu, text=text)
        found = re.findall(r"\\N\{([^}]*)\}", output)
        if len(found) != len(points):
            raise SystemExit(f"ICU gave {len(found)} names for {len(points)} code points")
        for p, name in zip(points, found):
            name = None if name.startswith("<") else name
            if name != unicode_names.get(p):
                differences.append(("icu", p, name, unicode_names.get(p)))
        print(f"icu: {len(points)} code points")

    # Java.
    if java_names:
        named = {}
        for line in java_names.read_text(encoding="utf-8").splitlines():
            point, name = line.split("\t")
            named[int(point, 16)] = name
        for p in range(0x110000):
            if p in named:
                if named[p] != java.get(p):
                    differences.append(("java", p, named[p], java.get(p)))
            elif p in java and age_of(p) <= (13, 0):
                differences.append(("java", p, None, java.get(p)))
        print(f"java: {len(named)} names")
    return differences


def digest(names, plane):
    value = 0
    for p in range(plane << 16, (plane + 1) << 16):
        name = names.get(p)
        if name is None:
            value = (value * 31 + 257) & 0xFFFFFFFF
            continue
        for byte in name.encode():
            value = (value * 31 + byte) & 0xFFFFFFFF
        value = (value * 31 + 256) & 0xFFFFFFFF
    return value


def write_test(unicode_names, java):
    lines = [
        "// test-options: --gpu=off --max-steps 40000000000",
        "// Generated by tests/stdlib/character/generators/names.py; do not edit.",
        "let character = import:Std.Core.Character;",
        "let hashing = import:Std.Core.Hash;",
        "let.promote _ = import:Std.Core.Optional;",
        "let optional = import:Std.Core.Optional;",
        "let test = import:Std.Test;",
        "",
        "// Every code point of each plane (Stdlib.md section 8.5): the digest of get_name and of unicode_name over",
        "// the plane (the bytes of each name and then 256, or 257 for None, combined as Hash.combine) equals the",
        "// digest the generator computed from the Unicode 17.0.0 files, and code_point_of gives the code point back",
        "// for every name of both kinds.",
        "let CharacterNameTests = module (",
        "    /// @brief The digest of one name, continuing `digest`.",
        "    let add = func(digest: uint32, name: Optional<string>) -> uint32 => match (name) {",
        "        Optional.None => hashing.combine(digest, 257),",
        "        Optional.Some(text) => named(digest, text),",
        "    };",
        "",
        "    let named = func(digest: uint32, text: string) -> uint32 {",
        "        let result = digest;",
        "        for (let i = 0; i < text.byte_length; i += 1) {",
        "            result = hashing.combine(result, text.byte_at(i) as uint32);",
        "        }",
        "        return hashing.combine(result, 256);",
        "    };",
        "",
        "    /// @brief The number of names in a plane that code_point_of does not give back, and the first one.",
        "    let round_trip = func(plane: uint32) -> string {",
        "        let failures = 0;",
        "        let first = \"\";",
        "        for (let p: uint32 = plane * 65536; p < (plane + 1) * 65536; p += 1) {",
        "            last names: Optional<string>[] = [character.get_name(p), character.unicode_name(p)];",
        "            for (let name in names) {",
        "                last text = optional.value_or(name, \"\");",
        "                if (text != \"\" && character.code_point_of(text) != p) {",
        "                    failures += 1;",
        "                    first = first == \"\" ? text : first;",
        "                }",
        "            }",
        "        }",
        "        return failures == 0 ? \"\" : failures.to_string() + \" names, first \" + first;",
        "    };",
        "",
        "    /// @brief Checks one plane's digests and round trip.",
        "    let check_plane = func(plane: uint32, java: uint32, unicode: uint32) -> void {",
        "        let java_digest: uint32 = 0;",
        "        let unicode_digest: uint32 = 0;",
        "        for (let p: uint32 = plane * 65536; p < (plane + 1) * 65536; p += 1) {",
        "            java_digest = add(java_digest, character.get_name(p));",
        "            unicode_digest = add(unicode_digest, character.unicode_name(p));",
        "        }",
        "        test.assert_with(java_digest == java,",
        "            \"plane \" + plane.to_string() + \": get_name differs from the Unicode 17.0.0 data\");",
        "        test.assert_with(unicode_digest == unicode,",
        "            \"plane \" + plane.to_string() + \": unicode_name differs from the Unicode 17.0.0 data\");",
        "        last failed = round_trip(plane);",
        "        test.assert_with(failed == \"\", \"plane \" + plane.to_string() + \": code_point_of misses \" + failed);",
        "    };",
    ]
    for plane in range(17):
        lines += [
            "",
            f"    export let test_plane_{plane} = func() -> void {{",
            f"        check_plane({plane}, {digest(java, plane)}, {digest(unicode_names, plane)});",
            "    };",
        ]
    lines += [");", ""]
    path = ROOT / "tests/stdlib/character/names-tests.nra"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {path.relative_to(ROOT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--perl", default="perl", help="the perl whose Unicode::UCD is 17.0.0")
    parser.add_argument("--ucd", type=Path, required=True, help="the directory of the pinned UCD 17.0.0 files")
    parser.add_argument("--icu", help="a uconv command of ICU 77 or later, for the ICU cross-check")
    parser.add_argument("--java-names", type=Path, help="the output of JavaNames.java, for the Java cross-check")
    args = parser.parse_args()
    data = tables.parse_dump(tables.run_perl(args.perl))
    unicode_names, java = expected_names(args.ucd, data)
    differences = cross_check(data, unicode_names, java, args.perl, args.icu, args.java_names)
    if differences:
        for source, p, found, expected in differences[:50]:
            print(f"difference: {source} U+{p:04X} {found!r}, expected {expected!r}", file=sys.stderr)
        print(f"{len(differences)} differences", file=sys.stderr)
        return 1
    print("cross-checks passed")
    write_test(unicode_names, java)
    return 0


if __name__ == "__main__":
    sys.exit(main())
