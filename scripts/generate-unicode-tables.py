#!/usr/bin/env python3
"""Generate the Unicode 17.0.0 character property tables of Std.Core.Character.

The data comes from the Unicode Character Database 17.0.0 as Perl's
Unicode::UCD module exposes it (Perl 5.44 ships UCD 17.0.0; its public
prop_invmap and prop_invlist functions return each property as an inversion
map). The script runs a small Perl program, refuses any other Unicode
version, and checks the SHA-256 of the canonical dump against DUMP_SHA256, so
a regeneration reproduces the checked-in tables byte for byte.

    python3 scripts/generate-unicode-tables.py --perl "PERL COMMAND" --ucd DIRECTORY
    python3 scripts/generate-unicode-tables.py --perl "PERL COMMAND" --ucd DIRECTORY --check

PERL COMMAND is a perl 5.44 or later whose Unicode::UCD reports 17.0.0, for
example `flatpak run --command=perl org.freedesktop.Sdk//25.08`. DIRECTORY
holds the unmodified UCD 17.0.0 files of NAME_FILES (UnicodeData.txt,
extracted/DerivedName.txt, NameAliases.txt, Jamo.txt, and SpecialCasing.txt,
all in one directory), whose SHA-256 digests are pinned; the character names come from
them and not from Perl, whose Unicode::UCD 17.0.0 names U+18D00 through
U+18D1E "TANGUT IDEOGRAPH SUPPLEMENT-..." where DerivedName.txt has "TANGUT
IDEOGRAPH-...". --check writes nothing and exits with status 1 when a
generated file differs.

The output is ordinary Neura source under stdlib/std/core: the property
tables (internal/unicode-properties.nra), the name tables
(internal/unicode-names.nra), and the generated public modules
unicode-script.nra and unicode-block.nra. Running Neura needs neither Perl
nor UCD files. The Unicode License V3 notice is copied from
stdlib/std/text/internal/unicode-case-data.nra, which holds the same
version's case data.

Data: https://www.unicode.org/Public/17.0.0/ucd/
License: https://www.unicode.org/license.txt
"""

import argparse
import hashlib
import re
import shlex
import subprocess
import sys
from pathlib import Path

VERSION = "17.0.0"
ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "stdlib" / "std" / "core"
LICENSE_SOURCE = ROOT / "stdlib" / "std" / "text" / "internal" / "unicode-case-data.nra"

# SHA-256 of the canonical dump that PERL_PROGRAM prints for UCD 17.0.0.
DUMP_SHA256 = "645f2b282b2267639eddb933fee97f77c550b9c6bcc643dfebb8e8143bfdfd87"

# Age is not packed; tests/stdlib/character/generators/exhaustive.py reads it from the same dump.
MAP_PROPERTIES = ("gc", "bc", "sc", "blk", "nv", "suc", "slc", "stc", "age")
LIST_PROPERTIES = (
    "Alphabetic", "Uppercase", "Lowercase", "Ideographic", "ID_Start", "ID_Continue", "Bidi_Mirrored",
    "White_Space", "Emoji", "Emoji_Presentation", "Emoji_Modifier", "Emoji_Modifier_Base", "Emoji_Component",
    "Extended_Pictographic",
)

PERL_PROGRAM = r"""
use strict;
use warnings;
use Unicode::UCD qw(prop_invmap prop_invlist prop_values prop_value_aliases);
binmode STDOUT, ':raw';
print "VERSION ", Unicode::UCD::UnicodeVersion(), "\n";
for my $property (qw(%MAPS%)) {
    my ($list, $map, $format, $default) = prop_invmap($property);
    die "no $property" unless $list;
    print "MAP $property $format\n";
    for my $i (0 .. $#$list) {
        my $value = $map->[$i];
        $value = join(' ', @$value) if ref $value;
        print "$list->[$i]\t$value\n";
    }
}
for my $property (qw(%LISTS%)) {
    my @list = prop_invlist($property);
    die "no $property" unless @list;
    print "LIST $property\n";
    print "$_\n" for @list;
}
for my $property (qw(sc blk)) {
    print "ALIASES $property\n";
    for my $value (sort { $a cmp $b } prop_values($property)) {
        print join("\t", prop_value_aliases($property, $value)), "\n";
    }
}
print "END\n";
"""


def run_perl(command):
    program = (PERL_PROGRAM.replace("%MAPS%", " ".join(MAP_PROPERTIES))
               .replace("%LISTS%", " ".join(LIST_PROPERTIES)))
    result = subprocess.run(shlex.split(command) + ["-"], input=program.encode(), capture_output=True,
                            check=False)
    if result.returncode != 0:
        raise SystemExit(f"perl failed: {result.stderr.decode(errors='replace')}")
    return result.stdout


def parse_dump(dump):
    """The dump as {maps, lists, aliases}. Raises unless it is the pinned UCD 17.0.0 dump."""
    text = dump.decode("utf-8")
    lines = text.split("\n")
    if lines[0] != f"VERSION {VERSION}":
        raise SystemExit(f"Unicode::UCD reports {lines[0]!r}; the tables need {VERSION}")
    digest = hashlib.sha256(dump).hexdigest()
    if DUMP_SHA256 != "PENDING" and digest != DUMP_SHA256:
        raise SystemExit(f"the UCD dump has SHA-256 {digest}, not the pinned {DUMP_SHA256}")
    maps, lists, aliases = {}, {}, {}
    kind, current = None, None
    for line in lines[1:]:
        if line == "END":
            break
        if line.startswith("MAP "):
            _, name, form = line.split(" ")
            kind, current = "map", []
            maps[name] = {"format": form, "ranges": current}
        elif line.startswith("LIST "):
            kind, current = "list", []
            lists[line[5:]] = current
        elif line.startswith("ALIASES "):
            kind, current = "aliases", []
            aliases[line[8:]] = current
        elif kind == "map":
            start, value = line.split("\t", 1)
            current.append((int(start), value))
        elif kind == "list":
            current.append(int(line))
        elif line:
            current.append(tuple(line.split("\t")))
    return {"maps": maps, "lists": lists, "aliases": aliases, "sha256": digest}


def expand(data, name):
    """Per code point values of a map property: a dict with an entry for every code point whose value differs
    from the property's 'none' value (identity for case maps, NaN for nv). For gc, bc, sc, and blk the
    caller uses ranges() instead."""
    entry = data["maps"][name]
    ranges = entry["ranges"]
    result = {}
    for index, (start, value) in enumerate(ranges):
        end = ranges[index + 1][0] if index + 1 < len(ranges) else 0x110000
        if entry["format"] == "a":
            if value == "0":
                continue
            for point in range(start, end):
                result[point] = int(value) + point - start
        elif entry["format"] == "ar":
            if value == "NaN":
                continue
            for point in range(start, end):
                result[point] = value if "/" in value else str(int(value) + point - start)
        else:
            raise ValueError(f"unexpected format {entry['format']} for {name}")
    return result


def value_at(ranges, point):
    low, high = 0, len(ranges)
    while low < high:
        middle = (low + high) // 2
        if ranges[middle][0] <= point:
            low = middle + 1
        else:
            high = middle
    return ranges[low - 1][1]


def in_list(boundaries, point):
    low, high = 0, len(boundaries)
    while low < high:
        middle = (low + high) // 2
        if boundaries[middle] <= point:
            low = middle + 1
        else:
            high = middle
    return low % 2 == 1


# ---------------------------------------------------------------------------------------------------------
# Value orders. The enums of Std.Core.Character declare their variants in these orders.

CATEGORIES = (
    ("Lu", "UppercaseLetter"), ("Ll", "LowercaseLetter"), ("Lt", "TitlecaseLetter"), ("Lm", "ModifierLetter"),
    ("Lo", "OtherLetter"), ("Mn", "NonspacingMark"), ("Mc", "SpacingMark"), ("Me", "EnclosingMark"),
    ("Nd", "DecimalNumber"), ("Nl", "LetterNumber"), ("No", "OtherNumber"), ("Pc", "ConnectorPunctuation"),
    ("Pd", "DashPunctuation"), ("Ps", "OpenPunctuation"), ("Pe", "ClosePunctuation"),
    ("Pi", "InitialPunctuation"), ("Pf", "FinalPunctuation"), ("Po", "OtherPunctuation"), ("Sm", "MathSymbol"),
    ("Sc", "CurrencySymbol"), ("Sk", "ModifierSymbol"), ("So", "OtherSymbol"), ("Zs", "SpaceSeparator"),
    ("Zl", "LineSeparator"), ("Zp", "ParagraphSeparator"), ("Cc", "Control"), ("Cf", "Format"),
    ("Cs", "Surrogate"), ("Co", "PrivateUse"), ("Cn", "Unassigned"),
)
BIDI_CLASSES = (
    ("L", "LeftToRight"), ("R", "RightToLeft"), ("AL", "ArabicLetter"), ("EN", "EuropeanNumber"),
    ("ES", "EuropeanSeparator"), ("ET", "EuropeanTerminator"), ("AN", "ArabicNumber"),
    ("CS", "CommonSeparator"), ("NSM", "NonspacingMark"), ("BN", "BoundaryNeutral"),
    ("B", "ParagraphSeparator"), ("S", "SegmentSeparator"), ("WS", "WhiteSpace"), ("ON", "OtherNeutral"),
    ("LRE", "LeftToRightEmbedding"), ("LRO", "LeftToRightOverride"), ("RLE", "RightToLeftEmbedding"),
    ("RLO", "RightToLeftOverride"), ("PDF", "PopDirectionalFormat"), ("LRI", "LeftToRightIsolate"),
    ("RLI", "RightToLeftIsolate"), ("FSI", "FirstStrongIsolate"), ("PDI", "PopDirectionalIsolate"),
)


def variant_name(text):
    """A PascalCase enum variant from a Unicode value name: words split at spaces, underscores and hyphens;
    an all-capital word keeps only its first capital."""
    words = [word for word in re.split(r"[ _\-]", text) if word]
    parts = []
    for word in words:
        if len(word) > 1 and word.isupper():
            word = word[0] + word[1:].lower()
        parts.append(word[0].upper() + word[1:])
    name = "".join(parts)
    if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", name):
        raise ValueError(f"cannot name {text!r}")
    return name


def scripts(data):
    """[(long name, ISO 15924 code, aliases)] sorted by long name; the order of the UnicodeScript enum."""
    result = []
    for aliases in data["aliases"]["sc"]:
        code, long = aliases[0], aliases[1]
        result.append((long, code, tuple(aliases)))
    result.sort()
    used = {value for _, value in data["maps"]["sc"]["ranges"]}
    if not used <= {long for long, _, _ in result}:
        raise ValueError(f"scripts without aliases: {used - {long for long, _, _ in result}}")
    return result


def blocks(data):
    """[(Blocks.txt name, first, last, aliases)] in code point order; the UnicodeBlock enum puts NoBlock first."""
    alias_of = {}
    for aliases in data["aliases"]["blk"]:
        for alias in aliases:
            alias_of[loose(alias)] = aliases
    ranges = data["maps"]["blk"]["ranges"]
    result = []
    for index, (start, value) in enumerate(ranges):
        if value == "No_Block":
            continue
        end = ranges[index + 1][0] - 1 if index + 1 < len(ranges) else 0x10FFFF
        result.append((value, start, end, alias_of[loose(value)]))
    return result


def loose(name):
    """UAX #44 LM3 loose matching: ignore case, whitespace, underscores, and hyphens."""
    return re.sub(r"[\s_\-]", "", name).lower()


# ---------------------------------------------------------------------------------------------------------
# Packing. A table is a string of printable ASCII: each byte holds six bits as its value minus 48, so the
# digits run from '0' (0) through 'o' (63). Words are big-endian groups of digits.

def digits(value, width):
    if value < 0 or value >= 1 << (6 * width):
        raise ValueError(f"{value} does not fit {width} digits")
    return "".join(chr(48 + ((value >> (6 * (width - 1 - i))) & 63)) for i in range(width))


def neura_string(text):
    return '"' + text.replace("\\", "\\\\") + '"'


def range_table(ranges):
    """Five digits per range: the first code point shifted left 9 bits, or'ed with a value below 512.
    Ranges with equal values are merged; the first range starts at 0."""
    merged = []
    for start, value in ranges:
        if not 0 <= value < 512:
            raise ValueError(f"range value {value} does not fit 9 bits")
        if merged and merged[-1][1] == value:
            continue
        merged.append((start, value))
    assert merged[0][0] == 0
    return "".join(digits(start << 9 | value, 5) for start, value in merged), len(merged)


def boundary_table(boundaries):
    """Four digits per boundary of an inversion list: odd counts of boundaries at or below a code point mean
    the code point is in the set."""
    return "".join(digits(boundary, 4) for boundary in boundaries if boundary <= 0x10FFFF + 1)


def case_tables(data):
    """The case range table (value: index of a delta triple) and the triples (3 signed deltas, 4 digits each,
    biased by 2^23)."""
    upper, lower, title = expand(data, "suc"), expand(data, "slc"), expand(data, "stc")
    mapped = upper.keys() | lower.keys() | title.keys()
    triples = [(0, 0, 0)]
    index_of = {(0, 0, 0): 0}
    changes = {0: 0}
    for point in sorted(mapped):
        triple = (upper.get(point, point) - point, lower.get(point, point) - point, title.get(point, point) - point)
        if triple not in index_of:
            index_of[triple] = len(triples)
            triples.append(triple)
        changes[point] = index_of[triple]
        if point + 1 not in mapped:
            changes[point + 1] = 0
    ranges = sorted(changes.items())
    packed, count = range_table(ranges)
    deltas = "".join(digits(delta + (1 << 23), 4) for triple in triples for delta in triple)
    return packed, count, deltas, len(triples)


def java_numeric(value):
    """Character.numeric_value for a Unicode Numeric_Value: the value when it is an integer from 0 through
    2^31 - 1, otherwise -2."""
    if "/" in value:
        return -2
    number = int(value)
    return number if 0 <= number <= 2147483647 else -2


def numeric_tables(data):
    """The numeric range table (value: index into the value list, 0 for none) and the value list: six digits
    per entry holding numeric_value + 2, then the Numeric_Value text after a separator string."""
    numeric = expand(data, "nv")
    values = [None]
    index_of = {None: 0}
    changes = {0: 0}
    for point in sorted(numeric):
        value = numeric[point]
        if value not in index_of:
            index_of[value] = len(values)
            values.append(value)
        changes[point] = index_of[value]
        if point + 1 not in numeric:
            changes[point + 1] = 0
    ranges = sorted(changes.items())
    packed, count = range_table(ranges)
    integers = "".join(digits(java_numeric(value) + 2, 6) for value in values[1:])
    return packed, count, integers, ";".join(values[1:]), len(values) - 1


def enum_ranges(data, name, order):
    index = {code: i for i, code in enumerate(order)}
    return [(start, index[value]) for start, value in data["maps"][name]["ranges"]]


# ---------------------------------------------------------------------------------------------------------
# Output.

def license_notice():
    lines = LICENSE_SOURCE.read_text(encoding="utf-8").splitlines()
    start = lines.index("// UNICODE LICENSE V3")
    end = lines.index("", start)
    return lines[start:end]


def header(description):
    return [
        f"// Generated from the Unicode Character Database {VERSION} by scripts/generate-unicode-tables.py.",
        "// Do not edit: change the generator and regenerate.",
        f"// {description}",
        "//",
        *license_notice(),
        "",
    ]


def generate(data, names):
    files = {}
    files["internal/unicode-names.nra"], stats = names_module(names)
    print("names: " + ", ".join(f"{k} {v}" for k, v in stats.items()), file=sys.stderr)
    files["internal/unicode-properties.nra"] = properties_module(data)
    files["unicode-script.nra"] = script_module(data)
    files["unicode-block.nra"] = block_module(data)
    return files


def properties_module(data):
    lists = data["lists"]
    category, category_count = range_table(enum_ranges(data, "gc", [c for c, _ in CATEGORIES]))
    bidi, bidi_count = range_table(enum_ranges(data, "bc", [c for c, _ in BIDI_CLASSES]))
    case, case_count, case_deltas, triple_count = case_tables(data)
    numeric, numeric_count, numeric_integers, numeric_texts, value_count = numeric_tables(data)
    binary = [
        ("Alphabetic", "Alphabetic", "The Alphabetic property."),
        ("Uppercase", "Uppercase", "The Uppercase property: Lu and Other_Uppercase."),
        ("Lowercase", "Lowercase", "The Lowercase property: Ll and Other_Lowercase."),
        ("Ideographic", "Ideographic", "The Ideographic property."),
        ("IdStart", "ID_Start", "The ID_Start property of UAX #31."),
        ("IdContinue", "ID_Continue", "The ID_Continue property of UAX #31."),
        ("BidiMirrored", "Bidi_Mirrored", "The Bidi_Mirrored property."),
        ("WhiteSpace", "White_Space", "The White_Space property."),
        ("Emoji", "Emoji", "The Emoji property of UTS #51."),
        ("EmojiPresentation", "Emoji_Presentation", "The Emoji_Presentation property of UTS #51."),
        ("EmojiModifier", "Emoji_Modifier", "The Emoji_Modifier property of UTS #51."),
        ("EmojiModifierBase", "Emoji_Modifier_Base", "The Emoji_Modifier_Base property of UTS #51."),
        ("EmojiComponent", "Emoji_Component", "The Emoji_Component property of UTS #51."),
        ("ExtendedPictographic", "Extended_Pictographic", "The Extended_Pictographic property of UTS #51."),
    ]
    out = header("Range tables: five digits per range, boundary tables: four digits per boundary; see the "
                 "generator.")
    out += [
        "/// @brief Generated Unicode property tables for Std.Core.Character. Each byte of a table holds six bits",
        "/// as its value minus 48. A range table holds five-digit words, the first code point of a range shifted",
        "/// left 9 bits or'ed with the range's value; a boundary table holds the four-digit boundaries of an",
        "/// inversion list. Std.Core.Internal.UnicodeLookup reads them.",
        "export let UnicodeProperties = module (",
        f"    /// @brief The Unicode version of every table: {VERSION}.",
        f'    export const Version: string = "{VERSION}";',
        "",
        f"    /// @brief General_Category ranges ({category_count}); values in the order of Character.GeneralCategory.",
        f"    export const Category: string = {neura_string(category)};",
        "",
        f"    /// @brief Bidi_Class ranges ({bidi_count}), including the defaults of unassigned code points; values",
        "    /// in the order of Character.BidiClass.",
        f"    export const Bidi: string = {neura_string(bidi)};",
        "",
        f"    /// @brief Simple case mapping ranges ({case_count}); a value indexes CaseDeltas.",
        f"    export const Case: string = {neura_string(case)};",
        "",
        f"    /// @brief {triple_count} triples of four-digit deltas biased by 2^23: Simple_Uppercase_Mapping,",
        "    /// Simple_Lowercase_Mapping, and Simple_Titlecase_Mapping minus the code point.",
        f"    export const CaseDeltas: string = {neura_string(case_deltas)};",
        "",
        f"    /// @brief Numeric_Value ranges ({numeric_count}); a value indexes NumericIntegers from 1, and 0 means",
        "    /// no numeric value.",
        f"    export const Numeric: string = {neura_string(numeric)};",
        "",
        f"    /// @brief {value_count} six-digit entries: Character.numeric_value plus 2 for each numeric value.",
        f"    export const NumericIntegers: string = {neura_string(numeric_integers)};",
        "",
        "    /// @brief The same values as Numeric_Value text (an integer or a fraction), separated by semicolons.",
        f"    export const NumericTexts: string = {neura_string(numeric_texts)};",
    ]
    for name, property, description in binary:
        boundaries = lists[property]
        out += [
            "",
            f"    /// @brief {description} {len(boundaries)} boundaries.",
            f"    export const {name}: string = {neura_string(boundary_table(boundaries))};",
        ]
    out += [");", ""]
    return "\n".join(out)


def wrap_items(items, indent, width=116):
    lines, line = [], indent
    for item in items:
        piece = item + ","
        if len(line) + len(piece) + 1 > width and line.strip():
            lines.append(line.rstrip())
            line = indent
        line += piece + " "
    if line.strip():
        lines.append(line.rstrip().rstrip(","))
    return lines


def variant_tree(enum, variants, low, high, indent):
    """Statements returning variants[index] for low <= index < high by bisection; the language has no
    conversion from an ordinal to a variant."""
    pad = " " * indent
    if high - low == 1:
        return [f"{pad}return {enum}.{variants[low]};"]
    middle = (low + high) // 2
    return ([f"{pad}if (index < {middle}) {{"] + variant_tree(enum, variants, low, middle, indent + 4) +
            [f"{pad}}}"] + variant_tree(enum, variants, middle, high, indent))


def string_list(names):
    """Names joined into one string and the three-digit offsets of each name's start, plus the end."""
    offsets, position = [], 0
    for name in names:
        if not name.isascii() or '"' in name or "\\" in name:
            raise ValueError(f"unexpected name {name!r}")
        offsets.append(position)
        position += len(name)
    offsets.append(position)
    return "".join(names), "".join(digits(offset, 3) for offset in offsets)


def enum_module(enum, summary, details, variants, ranges, text_lists, loose_names, extra):
    """A generated public module with an enum, its range table, text lists, and the shared functions."""
    table, count = range_table(ranges)
    loose_text, loose_offsets = string_list([name for name, _ in loose_names])
    out = [
        "let lookup = import:Std.Core.Internal.UnicodeLookup;",
        "let.promote _ = import:Std.Core.Optional;",
        "",
        f"/// @brief {summary}",
        "///",
        *[f"/// {line}" for line in details],
        f"/// @see [Std.Core.{enum}](https://github.com/Kayyo321/neura/wiki/Stdlib-Core-{enum})",
        '#Since("0.3.0")',
        f"export let {enum} = module (",
        f"    /// @brief {text_lists[0][2]}",
        f"    export let {enum} = enum (",
        *wrap_items(variants, "        "),
        "    );",
        "",
        f"    /// @brief {count} ranges in the range-table format of Std.Core.Internal.UnicodeLookup.",
        f"    const Table: string = {neura_string(table)};",
    ]
    for const, names, _ in text_lists[1:]:
        text, offsets = string_list(names)
        out += [
            "",
            f"    /// @brief {len(names)} texts in declaration order, joined; {const}Offsets has their starts.",
            f"    const {const}: string = {neura_string(text)};",
            f"    /// @brief The three-digit start of each text in {const}, then its length.",
            f"    const {const}Offsets: string = {neura_string(offsets)};",
        ]
    out += [
        "",
        "    /// @brief Every name, code, and alias in loose form (UAX #44 LM3), sorted and joined.",
        f"    const LooseNames: string = {neura_string(loose_text)};",
        "    /// @brief The three-digit start of each loose name, then the length.",
        f"    const LooseOffsets: string = {neura_string(loose_offsets)};",
        "    /// @brief The two-digit variant ordinal of each loose name.",
        f"    const LooseVariants: string = {neura_string(''.join(digits(i, 2) for _, i in loose_names))};",
        "",
        *extra,
        "",
        f"    /// @brief The {enum} with this name, compared with UAX #44 loose matching: ASCII case, spaces,",
        "    /// underscores, and hyphens are ignored. None for any other name.",
        "    #[Pure, ThreadSafe]",
        f"    export let for_name = func(text: string) -> Optional<{enum}> {{",
        "        last index = lookup.find_loose(LooseNames, LooseOffsets, text);",
        f"        return index < 0 ? Optional<{enum}>.None",
        f"            : Optional<{enum}>.Some(variant(lookup.word(LooseVariants, index * 2, 2) as int));",
        "    };",
        "",
        "    /// @brief Every variant in declaration order.",
        "    #[Pure, ThreadSafe]",
        f"    export let all = func() -> {enum}[list] {{",
        f"        last result: {enum}[list] = new;",
        f"        for (let index = 0; index < {len(variants)}; index += 1) {{",
        "            result.append(variant(index));",
        "        }",
        "        return result;",
        "    };",
        "",
        "    /// @brief The variant with a declaration ordinal.",
        "    #Pure",
        f"    let variant = func(index: int) -> {enum} {{",
        *variant_tree(enum, variants, 0, len(variants), 8),
        "    };",
        ");",
        "",
    ]
    return out


def script_module(data):
    entries = scripts(data)
    index = {long: i for i, (long, _, _) in enumerate(entries)}
    ranges = [(start, index[value]) for start, value in data["maps"]["sc"]["ranges"]]
    variants = [variant_name(long) for long, _, _ in entries]
    assert len(set(variants)) == len(variants)
    loose_names = sorted(set((loose(alias), i) for i, (_, _, aliases) in enumerate(entries) for alias in aliases))
    unknown = index["Unknown"]
    extra = [
        "    /// @brief The script of a code point; Unknown for unassigned code points, noncharacters, and values",
        "    /// above 0x10FFFF.",
        "    #[Pure, ThreadSafe]",
        "    export let of = func(code_point: uint32) -> UnicodeScript =>",
        f"        variant(code_point > 1114111 ? {unknown} : lookup.range_value(Table, code_point));",
        "",
        "    /// @brief The Unicode long name, such as `Old_Italic`.",
        "    #[Pure, ThreadSafe]",
        "    export let name = func(script: UnicodeScript) -> string =>",
        "        lookup.entry(LongNames, LongNamesOffsets, script as int);",
        "",
        "    /// @brief The four-letter ISO 15924 code, such as `Ital`.",
        "    #[Pure, ThreadSafe]",
        "    export let code = func(script: UnicodeScript) -> string =>",
        "        lookup.entry(Codes, CodesOffsets, script as int);",
    ]
    out = header("The Script property (Scripts.txt).")
    out += enum_module(
        "UnicodeScript",
        f"The Unicode Script property ({VERSION}), Java's Character.UnicodeScript.",
        ["The enum UnicodeScript has one variant per script value, sorted by the value's long name, including",
         "Common, Inherited, and Unknown (the script of unassigned code points). Generated by",
         "scripts/generate-unicode-tables.py."],
        variants, ranges,
        [(None, None, "A value of the Script property."),
         ("LongNames", [long for long, _, _ in entries], None),
         ("Codes", [code for _, code, _ in entries], None)],
        loose_names, extra)
    return "\n".join(out)


def block_module(data):
    entries = blocks(data)
    changes = {0: 0}
    for i, (_, start, end, _) in enumerate(entries):
        changes[start] = i + 1
        if end + 1 <= 0x10FFFF and end + 1 not in changes:
            changes[end + 1] = 0
    ranges = sorted(changes.items())
    variants = ["NoBlock"] + [variant_name(name) for name, _, _, _ in entries]
    assert len(set(variants)) == len(variants)
    loose_names = sorted(set([(loose("No_Block"), 0)] + [
        (loose(alias), i + 1) for i, (name, _, _, aliases) in enumerate(entries) for alias in aliases + (name,)]))
    bounds = "".join(digits(value, 4) for _, start, end, _ in entries for value in (start, end))
    extra = [
        "    /// @brief The first and last code points of each block, four digits each, in declaration order from",
        "    /// the first block after NoBlock.",
        f"    const Bounds: string = {neura_string(bounds)};",
        "",
        "    /// @brief The block of a code point; NoBlock outside every block and above 0x10FFFF.",
        "    #[Pure, ThreadSafe]",
        "    export let of = func(code_point: uint32) -> UnicodeBlock =>",
        "        variant(code_point > 1114111 ? 0 : lookup.range_value(Table, code_point));",
        "",
        "    /// @brief The name in Blocks.txt, such as `Latin-1 Supplement`; `No_Block` for NoBlock.",
        "    #[Pure, ThreadSafe]",
        "    export let name = func(block: UnicodeBlock) -> string =>",
        "        lookup.entry(BlockNames, BlockNamesOffsets, block as int);",
        "",
        "    /// @brief The first code point of the block. Traps with \"invalid argument: NoBlock has no range\" for",
        "    /// NoBlock.",
        "    #[Pure, ThreadSafe]",
        "    export let first = func(block: UnicodeBlock) -> uint32 {",
        "        check_block(block);",
        "        return lookup.word(Bounds, ((block as int) - 1) * 8, 4) as uint32;",
        "    };",
        "",
        "    /// @brief The last code point of the block. Traps as `first` does for NoBlock.",
        "    #[Pure, ThreadSafe]",
        "    export let last = func(block: UnicodeBlock) -> uint32 {",
        "        check_block(block);",
        "        return lookup.word(Bounds, ((block as int) - 1) * 8 + 4, 4) as uint32;",
        "    };",
        "",
        "    /// @brief Traps for NoBlock.",
        "    #Pure",
        "    let check_block = func(block: UnicodeBlock) {",
        "        if (block == UnicodeBlock.NoBlock) {",
        '            trap_with_message("invalid argument: NoBlock has no range");',
        "        }",
        "    };",
    ]
    out = header("The Block property (Blocks.txt).")
    out += enum_module(
        "UnicodeBlock",
        f"The Unicode Block property ({VERSION}), Java's Character.UnicodeBlock.",
        [f"The enum UnicodeBlock has NoBlock and then the {len(entries)} blocks of Blocks.txt in code point order.",
         "Generated by scripts/generate-unicode-tables.py."],
        variants, ranges,
        [(None, None, "A value of the Block property; NoBlock for code points outside every block."),
         ("BlockNames", ["No_Block"] + [e[0] for e in entries], None)],
        loose_names, extra)
    return "\n".join(out)


# ---------------------------------------------------------------------------------------------------------
# Character names (Character.get_name, unicode_name, and code_point_of).

NAME_FILES = {
    "UnicodeData.txt": "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c",
    "DerivedName.txt": "019758bbe6c756c40fca6d505187ea660c5e195533e2ff2c841963a212c9d369",
    "NameAliases.txt": "793f6f1e4d15fd90f05ae66460191dc4d75d1fea90136a25f30dd6a4cb950eac",
    "Jamo.txt": "17bb07f5e37f995351ddcef393c04464a9e3891ed0c0bd56a03a5c5e400a6326",
    "SpecialCasing.txt": "efc25faf19de21b92c1194c111c932e03d2a5eaf18194e33f1156e96de4c9588",
}

# Java's getName for the controls, which have no Unicode Name: the Unicode_1_Name of UnicodeData.txt, except
# these four, each a NameAliases.txt alias of the type given. Observed with OpenJDK 17.0.21
# (tests/stdlib/character/generators/JavaNames.java): U+0007 takes the abbreviation BEL because its
# Unicode_1_Name BELL is the Name of U+1F514, and three controls without a Unicode_1_Name take their figment
# alias. U+0084, which has neither, takes the block form like the other unnamed code points.
JAVA_CONTROL_NAMES = {
    0x0007: ("BEL", "abbreviation"),
    0x0080: ("PADDING CHARACTER", "figment"),
    0x0081: ("HIGH OCTET PRESET", "figment"),
    0x0099: ("SINGLE GRAPHIC CHARACTER INTRODUCER", "figment"),
}
CONTROLS = tuple(range(0x00, 0x20)) + tuple(range(0x7F, 0xA0))

# Java's UnicodeBlock constants that do not follow the Blocks.txt name; a block-form name would use the
# constant, so the generator refuses to give such a block a block-form name.
JAVA_LEGACY_BLOCKS = ("Greek and Coptic", "Cyrillic Supplement", "Combining Diacritical Marks for Symbols")

HANGUL_FIRST, HANGUL_LAST = 0xAC00, 0xD7A3
NAME_BLOCK = 32           # names per front-coding block
NAME_WORD = re.compile(r"[A-Z0-9-]+")
# The bytes that start a dictionary code in the name stream: printable ASCII except the name alphabet
# (A-Z, 0-9, hyphen, space), the record end '.', and the quote and backslash.
NAME_LEADS = "".join(chr(b) for b in range(33, 127)
                     if not (chr(b).isdigit() or chr(b).isupper() or chr(b) in "-. \"\\"))


def ucd_records(path):
    for line in path.read_text(encoding="utf-8").splitlines():
        data = line.split("#", 1)[0].strip()
        if data:
            yield [field.strip() for field in data.split(";")]


def read_name_files(directory):
    """The pinned name files as {file name: path}; raises unless every digest matches."""
    paths = {}
    for name, digest in NAME_FILES.items():
        candidates = [directory / name, directory / "extracted" / name]
        path = next((c for c in candidates if c.exists()), None)
        if path is None:
            raise SystemExit(f"{name} is not in {directory}")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != digest:
            raise SystemExit(f"{path} has SHA-256 {actual}, not the pinned {digest}")
        paths[name] = path
    return paths


def name_data(directory, data):
    """Everything the name tables hold, derived from the pinned files and checked for consistency:
    {explicit, patterns, jamo, java_controls, fallback, assigned}."""
    paths = read_name_files(directory)
    # UnicodeData.txt: names, Unicode_1_Name, and the assigned code points (ranges written First/Last).
    listed, ranges_, assigned = {}, [], set()
    first = None
    for fields in ucd_records(paths["UnicodeData.txt"]):
        point, name, u1 = int(fields[0], 16), fields[1], fields[10]
        if name.endswith(", First>"):
            first = (point, name)
            continue
        if name.endswith(", Last>"):
            ranges_.append((first[0], point, first[1][1:-8]))
            assigned.update(range(first[0], point + 1))
            continue
        listed[point] = (name, u1)
        assigned.add(point)
    # The Perl General_Category agrees on the assigned code points.
    gc = data["maps"]["gc"]["ranges"]
    for start, end, _ in ranges_:
        assert value_at(gc, start) != "Cn"
    perl_assigned = set()
    for index, (start, value) in enumerate(gc):
        end = gc[index + 1][0] if index + 1 < len(gc) else 0x110000
        if value != "Cn":
            perl_assigned.update(range(start, end))
    if perl_assigned != assigned:
        raise SystemExit("UnicodeData.txt and the Perl General_Category disagree on the assigned code points")

    # DerivedName.txt: explicit names, NR2 patterns, and the Hangul syllables (listed one by one).
    explicit, patterns = {}, []
    for cps, name in ucd_records(paths["DerivedName.txt"]):
        if "*" in name:
            low, _, high = cps.partition("..")
            if not name.endswith("-*"):
                raise SystemExit(f"unexpected pattern {name}")
            patterns.append((int(low, 16), int(high or low, 16), name[:-1]))
        else:
            explicit[int(cps, 16)] = name

    # Jamo.txt short names and the NR1 rule of Unicode section 3.12.
    jamo = {int(fields[0], 16): fields[1] for fields in ucd_records(paths["Jamo.txt"])}
    leads = [jamo[0x1100 + i] for i in range(19)]
    vowels = [jamo[0x1161 + i] for i in range(21)]
    tails = [""] + [jamo[0x11A8 + i] for i in range(27)]
    for point in range(HANGUL_FIRST, HANGUL_LAST + 1):
        index = point - HANGUL_FIRST
        derived = "HANGUL SYLLABLE " + leads[index // 588] + vowels[index % 588 // 28] + tails[index % 28]
        if explicit.pop(point, None) != derived:
            raise SystemExit(f"DerivedName.txt does not give U+{point:04X} the NR1 name {derived}")

    # Every listed UnicodeData name that is not in angle brackets is the Name, explicit or by a pattern.
    def unicode_name(point):
        if point in explicit:
            return explicit[point]
        if HANGUL_FIRST <= point <= HANGUL_LAST:
            index = point - HANGUL_FIRST
            return "HANGUL SYLLABLE " + leads[index // 588] + vowels[index % 588 // 28] + tails[index % 28]
        for low, high, prefix in patterns:
            if low <= point <= high:
                return f"{prefix}{point:04X}"
        return None

    for point, (name, _) in listed.items():
        if not name.startswith("<") and unicode_name(point) != name:
            raise SystemExit(f"U+{point:04X}: UnicodeData.txt {name!r}, DerivedName.txt {unicode_name(point)!r}")
        if name.startswith("<") and name != "<control>":
            raise SystemExit(f"U+{point:04X}: unexpected {name}")
    for point in explicit:
        if point not in listed or listed[point][0].startswith("<"):
            raise SystemExit(f"U+{point:04X} has a DerivedName.txt name but no UnicodeData.txt name")

    # NameAliases.txt backs the Java control overrides.
    aliases = {}
    for point, alias, kind in ucd_records(paths["NameAliases.txt"]):
        aliases.setdefault(int(point, 16), []).append((alias, kind))
    java_controls = {}
    for point in CONTROLS:
        if listed[point][0] != "<control>":
            raise SystemExit(f"U+{point:04X} is not a control")
        if point in JAVA_CONTROL_NAMES:
            alias = JAVA_CONTROL_NAMES[point]
            if alias not in aliases.get(point, []):
                raise SystemExit(f"U+{point:04X}: {alias} is not in NameAliases.txt")
            java_controls[point] = alias[0]
        elif listed[point][1]:
            java_controls[point] = listed[point][1]
    assert sum(1 for c in java_controls.values() if c) == 64

    # Java's block form for every assigned code point without a Java name.
    blk = data["maps"]["blk"]["ranges"]
    fallback = {}
    for point in sorted(assigned):
        listed_name = listed.get(point, ("<range>", ""))[0]
        if point in java_controls or not listed_name.startswith("<"):
            continue
        block = value_at(blk, point)
        if block == "No_Block" or block in JAVA_LEGACY_BLOCKS:
            raise SystemExit(f"U+{point:04X} would take a block-form name in block {block}")
        fallback[point] = re.sub(r"[ _\-]", " ", block.upper())

    # The names are distinct across both members, so code_point_of is a function.
    owner = {}
    for point in sorted(assigned):
        for name in {unicode_name(point), java_name_of(point, listed, java_controls, fallback, unicode_name)}:
            if name is None:
                continue
            if owner.setdefault(name, point) != point:
                raise SystemExit(f"{name!r} names U+{owner[name]:04X} and U+{point:04X}")
    # The characters above U+007F whose full uppercase mapping (root rules: SpecialCasing.txt without
    # conditions, otherwise the simple mapping) is ASCII. Uppercasing is context-free under the root rules, so a
    # text with any other non-ASCII character uppercases to a non-ASCII text, which is no name.
    special_upper = {}
    for fields in ucd_records(paths["SpecialCasing.txt"]):
        if len(fields) > 4 and fields[4]:
            continue
        special_upper[int(fields[0], 16)] = "".join(chr(int(c, 16)) for c in fields[3].split())
    simple_upper = {}
    for fields in ucd_records(paths["UnicodeData.txt"]):
        if fields[12]:
            simple_upper[int(fields[0], 16)] = chr(int(fields[12], 16))
    ascii_uppers = []
    for point in range(0x80, 0x110000):
        mapped = special_upper.get(point, simple_upper.get(point, chr(point)))
        if mapped.isascii():
            ascii_uppers.append((point, mapped))
    return {"explicit": explicit, "patterns": patterns, "jamo": (leads, vowels, tails),
            "ascii_uppers": ascii_uppers,
            "java_controls": java_controls, "fallback": fallback, "assigned": assigned,
            "unicode_name": unicode_name,
            "java_name": lambda p: java_name_of(p, listed, java_controls, fallback, unicode_name)}


def java_name_of(point, listed, java_controls, fallback, unicode_name):
    if point in java_controls:
        return java_controls[point]
    if point in fallback:
        return f"{fallback[point]} {point:X}"
    if point in listed:
        return unicode_name(point)
    return None


def name_stream(explicit):
    """The front-coded name records in code point order, the dictionary, and its code widths.

    A record is one byte '0' + P, where P is how many leading words the name shares with the previous name of
    its block (0 for the first name of a block), then the remaining words, then '.'. A word is either its
    letters or a dictionary code; two words spelled out in a row are separated by a space. A code starts with
    a byte of NAME_LEADS: lead number l < W1 is word l; W1 <= l < W1 + W2 is word W1 + (l - W1) * 64 + d with
    one more digit d; any higher lead takes two more digits."""
    points = sorted(explicit)
    items, previous = [], []
    for index, point in enumerate(points):
        words = explicit[point].split(" ")
        if not all(NAME_WORD.fullmatch(w) for w in words):
            raise SystemExit(f"unexpected name {explicit[point]!r}")
        shared = 0
        if index % NAME_BLOCK:
            while shared < min(len(words), len(previous)) and words[shared] == previous[shared]:
                shared += 1
        items.append((shared, words[shared:]))
        previous = words
    frequency = {}
    for _, words in items:
        for word in words:
            frequency[word] = frequency.get(word, 0) + 1
    order = sorted(frequency, key=lambda w: (-frequency[w], w))
    best = None
    for w1 in range(len(NAME_LEADS) + 1):
        for w2 in range(len(NAME_LEADS) + 1 - w1):
            w3 = len(NAME_LEADS) - w1 - w2
            widths = [1] * w1 + [2] * (w2 * 64) + [3] * (w3 * 4096)
            chosen = {}
            for word in order:
                if len(chosen) == len(widths):
                    break
                width = widths[len(chosen)]
                if frequency[word] * len(word) > frequency[word] * width + len(word) + 1:
                    chosen[word] = len(chosen)
            text = encode_records(items, chosen, w1, w2)
            size = len(text) + sum(len(w) + 1 for w in chosen)
            if best is None or size < best[0]:
                best = (size, text, list(chosen), w1, w2)
    _, text, dictionary, w1, w2 = best
    return points, text, dictionary, w1, w2


def encode_records(items, chosen, w1, w2):
    out = []
    for shared, words in items:
        out.append(chr(48 + shared))
        spelled = False
        for word in words:
            code = chosen.get(word)
            if code is None:
                if spelled:
                    out.append(" ")
                out.append(word)
                spelled = True
                continue
            spelled = False
            if code < w1:
                out.append(NAME_LEADS[code])
            elif code < w1 + w2 * 64:
                code -= w1
                out.append(NAME_LEADS[w1 + code // 64] + chr(48 + code % 64))
            else:
                code -= w1 + w2 * 64
                out.append(NAME_LEADS[w1 + w2 + code // 4096] + digits(code % 4096, 2))
        out.append(".")
    return "".join(out)


def decode_records(text, dictionary, w1, w2):
    """The inverse of encode_records, for the generator's own check."""
    names, previous, position = [], [], 0
    lead_index = {c: i for i, c in enumerate(NAME_LEADS)}
    while position < len(text):
        shared = ord(text[position]) - 48
        position += 1
        words = previous[:shared]
        word = None
        while True:
            c = text[position]
            if c == "." or c == " " or c in lead_index:
                if word is not None:
                    words.append(word)
                    word = None
                if c == ".":
                    position += 1
                    break
                if c == " ":
                    position += 1
                    continue
                lead = lead_index[c]
                if lead < w1:
                    code, position = lead, position + 1
                elif lead < w1 + w2:
                    code = w1 + (lead - w1) * 64 + ord(text[position + 1]) - 48
                    position += 2
                else:
                    code = (w1 + w2 * 64 + (lead - w1 - w2) * 4096 + (ord(text[position + 1]) - 48) * 64 +
                            ord(text[position + 2]) - 48)
                    position += 3
                words.append(dictionary[code])
            else:
                word = (word or "") + c
                position += 1
        names.append(" ".join(words))
        previous = words
    return names


def joined(texts, width=3):
    """Texts joined, with the start of each and then the end as `width`-digit words."""
    offsets, position = [], 0
    for text in texts:
        offsets.append(position)
        position += len(text)
    offsets.append(position)
    return "".join(texts), "".join(digits(o, width) for o in offsets)


def names_module(names):
    explicit = names["explicit"]
    points, stream, dictionary, w1, w2 = name_stream(explicit)
    if decode_records(stream, dictionary, w1, w2) != [explicit[p] for p in points]:
        raise SystemExit("the name stream does not decode to the names")
    block_offsets = []
    position = 0
    for index in range(0, len(points)):
        if index % NAME_BLOCK == 0:
            block_offsets.append(position)
        position = stream.index(".", position) + 1
    # Runs of consecutive explicitly named code points: first code point and first name index.
    runs = []
    for index, point in enumerate(points):
        if index == 0 or points[index - 1] != point - 1:
            runs.append((point, index))
    runs.append((0x110000, len(points)))
    # The first word of every name, sorted, with the blocks whose names start with it.
    blocks_of = {}
    for index, point in enumerate(points):
        blocks_of.setdefault(explicit[point].split(" ")[0], set()).add(index // NAME_BLOCK)
    first_words = sorted(blocks_of, key=lambda w: w.encode())
    word_blocks = [ "".join(digits(b, 2) for b in sorted(blocks_of[w])) for w in first_words]
    # Ranges without a stored name: NR1 (Hangul), NR2 patterns, and Java's block form.
    prefixes = sorted({prefix for _, _, prefix in names["patterns"]})
    java_blocks = sorted(set(names["fallback"].values()))
    kind = {}
    for low, high, prefix in names["patterns"]:
        for point in range(low, high + 1):
            kind[point] = [2 + prefixes.index(prefix), 0]
    for point in range(HANGUL_FIRST, HANGUL_LAST + 1):
        kind[point] = [1, 0]
    for point, block in names["fallback"].items():
        kind.setdefault(point, [0, 0])[1] = 1 + java_blocks.index(block)
    special = []
    for point in sorted(kind):
        if special and special[-1][1] == point - 1 and special[-1][2] == kind[point]:
            special[-1][1] = point
        else:
            special.append([point, point, kind[point]])
    assert NAME_BLOCK & (NAME_BLOCK - 1) == 0 and len(points) // NAME_BLOCK < 4096
    for low, high, (prefix, java) in special:
        assert prefix < 64 and java < 64
        if java == 0:
            assert all(names["java_name"](p) == names["unicode_name"](p) for p in (low, high))
    leads, vowels, tails = names["jamo"]
    controls = [names["java_controls"].get(p, "") for p in CONTROLS]
    lead_table = "".join(chr(48 + NAME_LEADS.index(chr(b))) if chr(b) in NAME_LEADS else " "
                         for b in range(32, 127))
    dictionary_text, dictionary_offsets = joined(dictionary, 4)
    first_text, first_offsets = joined(first_words, 4)
    blocks_text, blocks_offsets = joined(word_blocks, 4)
    prefix_text, prefix_offsets = joined(prefixes, 4)
    java_text, java_offsets = joined(java_blocks, 4)
    control_text, control_offsets = joined(controls, 4)
    jamo_text, jamo_offsets = joined(leads + vowels + tails, 4)
    upper_from = [chr(point) for point, _ in names["ascii_uppers"]]
    upper_offsets, position = [], 0
    for text in upper_from:
        upper_offsets.append(position)
        position += len(text.encode())
    upper_offsets.append(position)
    upper_to, upper_to_offsets = joined([mapped for _, mapped in names["ascii_uppers"]], 4)

    def const(name, value, kind="string"):
        text = neura_string(value) if kind == "string" else str(value)
        return f"    export const {name}: {kind} = {text};"

    # Every table holds four-digit numbers unless it says otherwise, so a reader multiplies by shifting.
    out = header("The Name property (DerivedName.txt, NameAliases.txt, Jamo.txt) and Java's getName names.")
    out += [
        "/// @brief Generated name tables for Std.Core.Character.get_name, unicode_name, and code_point_of, read by",
        "/// Std.Core.Internal.UnicodeNameLookup. Numbers are big-endian groups of four digits unless a comment says",
        "/// otherwise, each digit a byte whose value minus 48 holds six bits. A joined list of texts has an",
        "/// offsets table with the start of each text and then the end. Only the members that read names import",
        "/// this module.",
        "export let UnicodeNames = module (",
        f"    /// @brief {len(points)} stored names in code point order, front-coded in blocks of {NAME_BLOCK}: a",
        "    /// record is '0' plus the number of leading words shared with the previous name of its block, the",
        "    /// other words spelled out (two in a row separated by a space) or as dictionary codes, and '.'.",
        const("Stream", stream),
        f"    /// @brief The start of each block of {NAME_BLOCK} records in Stream.",
        const("BlockOffsets", "".join(digits(o, 4) for o in block_offsets)),
        "    /// @brief The base-2 logarithm of the number of records in a block.",
        const("BlockShift", NAME_BLOCK.bit_length() - 1, "int"),
        f"    /// @brief The number of stored names.",
        const("RecordCount", len(points), "int"),
        "",
        f"    /// @brief {len(dictionary)} dictionary words, most frequent first, joined, and their offsets.",
        const("Dictionary", dictionary_text),
        const("DictionaryOffsets", dictionary_offsets),
        "    /// @brief For each byte from 32 through 126, '0' plus its lead number when it starts a dictionary code,",
        "    /// otherwise a space. Leads below OneByteLeads are one-byte codes (the word number is the lead number),",
        "    /// the next TwoByteLeads take one more digit, and the rest take two more digits.",
        const("Leads", lead_table),
        const("OneByteLeads", w1, "int"),
        const("TwoByteLeads", w2, "int"),
        "",
        f"    /// @brief The first code points of the {len(runs) - 1} runs of consecutive stored names, then 0x110000.",
        const("RunStarts", "".join(digits(p, 4) for p, _ in runs)),
        "    /// @brief The record number of the first name of each run, then the number of records.",
        const("RunRecords", "".join(digits(i, 4) for _, i in runs)),
        f"    /// @brief The number of runs.",
        const("RunCount", len(runs) - 1, "int"),
        "",
        f"    /// @brief The {len(first_words)} distinct first words of the stored names, sorted bytewise, joined, and",
        "    /// their offsets.",
        const("FirstWords", first_text),
        const("FirstWordOffsets", first_offsets),
        "    /// @brief For each first word, the two-digit numbers of the blocks holding a name that starts with it,",
        "    /// joined, and their offsets.",
        const("FirstWordBlocks", blocks_text),
        const("FirstWordBlockOffsets", blocks_offsets),
        "",
        f"    /// @brief The first and last code points of the {len(special)} ranges whose names are not stored.",
        const("SpecialFirsts", "".join(digits(low, 4) for low, _, _ in special)),
        const("SpecialLasts", "".join(digits(high, 4) for _, high, _ in special)),
        "    /// @brief Two one-digit numbers for each range: its Unicode names (0 none, 1 Hangul syllables, 2 or more",
        "    /// the prefix numbered 2 less followed by the code point in hex) and its Java names (0 the Unicode",
        "    /// names, otherwise the block form with the Java block name numbered 1 less).",
        const("SpecialKinds", "".join(digits(prefix, 1) + digits(java, 1) for _, _, (prefix, java) in special)),
        const("SpecialCount", len(special), "int"),
        f"    /// @brief The {len(prefixes)} prefixes of the patterned names (NR2), joined, and their offsets.",
        const("Prefixes", prefix_text),
        const("PrefixOffsets", prefix_offsets),
        f"    /// @brief The {len(java_blocks)} Java block names of the block form, joined, and their offsets.",
        const("JavaBlocks", java_text),
        const("JavaBlockOffsets", java_offsets),
        "",
        "    /// @brief Java's names of the controls U+0000 through U+001F and U+007F through U+009F in order, empty",
        "    /// for U+0084, joined, and their offsets.",
        const("Controls", control_text),
        const("ControlOffsets", control_offsets),
        "",
        "    /// @brief The Jamo short names: 19 leading consonants, 21 vowels, and 28 trailing consonants (the first",
        "    /// empty), joined, and their offsets.",
        const("Jamo", jamo_text),
        const("JamoOffsets", jamo_offsets),
        "",
        f"    /// @brief The {len(upper_from)} characters above U+007F whose uppercase mapping under the root rules is",
        "    /// ASCII, joined, with the offsets of their UTF-8 bytes, and those mappings, joined, and their offsets.",
        const("UpperFrom", "".join(upper_from)),
        const("UpperFromOffsets", "".join(digits(o, 4) for o in upper_offsets)),
        const("UpperTo", upper_to),
        const("UpperToOffsets", upper_to_offsets),
        ");",
        "",
    ]
    stats = {"names": len(points), "stream": len(stream), "dictionary": len(dictionary),
             "runs": len(runs) - 1, "first_words": len(first_words), "special": len(special)}
    return "\n".join(out), stats


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--perl", default="perl", help="the perl command whose Unicode::UCD is 17.0.0")
    parser.add_argument("--ucd", type=Path, required=True,
                        help="the directory of the UCD 17.0.0 files of NAME_FILES")
    parser.add_argument("--check", action="store_true", help="compare instead of writing")
    parser.add_argument("--dump", type=Path, help="also write the canonical dump to this file")
    args = parser.parse_args()
    dump = run_perl(args.perl)
    if args.dump:
        args.dump.write_bytes(dump)
    data = parse_dump(dump)
    if DUMP_SHA256 == "PENDING":
        print(f"dump SHA-256 {data['sha256']}", file=sys.stderr)
    stale = []
    for relative, text in generate(data, name_data(args.ucd, data)).items():
        path = CORE / relative
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                stale.append(relative)
        else:
            path.write_text(text, encoding="utf-8")
            print(f"wrote {path.relative_to(ROOT)} ({len(text.encode())} bytes)")
    if stale:
        print("out of date: " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
