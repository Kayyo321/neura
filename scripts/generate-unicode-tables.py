#!/usr/bin/env python3
"""Generate the Unicode 17.0.0 character property tables of Std.Core.Character.

The data comes from the Unicode Character Database 17.0.0 as Perl's
Unicode::UCD module exposes it (Perl 5.44 ships UCD 17.0.0; its public
prop_invmap and prop_invlist functions return each property as an inversion
map). The script runs a small Perl program, refuses any other Unicode
version, and checks the SHA-256 of the canonical dump against DUMP_SHA256, so
a regeneration reproduces the checked-in tables byte for byte.

    python3 scripts/generate-unicode-tables.py --perl "PERL COMMAND"
    python3 scripts/generate-unicode-tables.py --perl "PERL COMMAND" --check

PERL COMMAND is a perl 5.44 or later whose Unicode::UCD reports 17.0.0, for
example `flatpak run --command=perl org.freedesktop.Sdk//25.08`. --check
writes nothing and exits with status 1 when a generated file differs.

The output is ordinary Neura source under stdlib/std/core: the property
tables (internal/unicode-properties.nra) and the generated public modules
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


def generate(data):
    files = {}
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--perl", default="perl", help="the perl command whose Unicode::UCD is 17.0.0")
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
    for relative, text in generate(data).items():
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
