# `Std.Core.UnicodeScript`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.UnicodeScript`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/unicode-script.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.UnicodeScript`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UnicodeScript** · [`export let UnicodeScript = enum ( Adlam, Ahom, AnatolianHieroglyphs, Arabic, Armenian, Avestan, Balinese, Bamum, BassaVah, Batak, Bengali, BeriaErfe, Bhaiksuki, Bopomofo, Brahmi, Braille, Buginese, Buhid, CanadianAboriginal, Carian, CaucasianAlbanian, Chakma, Cham, Cherokee, Chorasmian, Common, Coptic, Cuneiform, Cypriot, CyproMinoan, Cyrillic, Deseret, Devanagari, DivesAkuru, Dogra, Duployan, EgyptianHieroglyphs, Elbasan, Elymaic, Ethiopic, Garay, Georgian, Glagolitic, Gothic, Grantha, Greek, Gujarati, GunjalaGondi, Gurmukhi, GurungKhema, Han, Hangul, HanifiRohingya, Hanunoo, Hatran, Hebrew, Hiragana, ImperialAramaic, Inherited, InscriptionalPahlavi, InscriptionalParthian, Javanese, Kaithi, Kannada, Katakana, Kawi, KayahLi, Kharoshthi, KhitanSmallScript, Khmer, Khojki, Khudawadi, KiratRai, Lao, Latin, Lepcha, Limbu, LinearA, LinearB, Lisu, Lycian, Lydian, Mahajani, Makasar, Malayalam, Mandaic, Manichaean, Marchen, MasaramGondi, Medefaidrin, MeeteiMayek, MendeKikakui, MeroiticCursive, MeroiticHieroglyphs, Miao, Modi, Mongolian, Mro, Multani, Myanmar, Nabataean, NagMundari, Nandinagari, NewTaiLue, Newa, Nko, Nushu, NyiakengPuachueHmong, Ogham, OlChiki, OlOnal, OldHungarian, OldItalic, OldNorthArabian, OldPermic, OldPersian, OldSogdian, OldSouthArabian, OldTurkic, OldUyghur, Oriya, Osage, Osmanya, PahawhHmong, Palmyrene, PauCinHau, PhagsPa, Phoenician, PsalterPahlavi, Rejang, Runic, Samaritan, Saurashtra, Sharada, Shavian, Siddham, Sidetic, SignWriting, Sinhala, Sogdian, SoraSompeng, Soyombo, Sundanese, Sunuwar, SylotiNagri, Syriac, Tagalog, Tagbanwa, TaiLe, TaiTham, TaiViet, TaiYo, Takri, Tamil, Tangsa, Tangut, Telugu, Thaana, Thai, Tibetan, Tifinagh, Tirhuta, Todhri, TolongSiki, Toto, TuluTigalari, Ugaritic, Unknown, Vai, Vithkuqi, Wancho, WarangCiti, Yezidi, Yi, ZanabazarSquare )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/unicode-script.nra#L57)
  A value of the Script property.
- **UnicodeScript** · [`export let of = func(code_point: uint32) -> UnicodeScript`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/unicode-script.nra#L101)
  The script of a code point; Unknown for unassigned code points, noncharacters, and values above 0x10FFFF. Tagged `#Pure`, `#ThreadSafe`.
- **UnicodeScript** · [`export let name = func(script: UnicodeScript) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/unicode-script.nra#L106)
  The Unicode long name, such as `Old_Italic`. Tagged `#Pure`, `#ThreadSafe`.
- **UnicodeScript** · [`export let code = func(script: UnicodeScript) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/unicode-script.nra#L111)
  The four-letter ISO 15924 code, such as `Ital`. Tagged `#Pure`, `#ThreadSafe`.
- **UnicodeScript** · [`export let for_name = func(text: string) -> Optional<UnicodeScript>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/unicode-script.nra#L117)
  The UnicodeScript with this name, compared with UAX #44 loose matching: ASCII case, spaces, underscores, and hyphens are ignored. None for any other name. Tagged `#Pure`, `#ThreadSafe`.
- **UnicodeScript** · [`export let all = func() -> UnicodeScript[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/unicode-script.nra#L125)
  Every variant in declaration order. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 8.5 `Character`

`Std.Core.Character` is Java's `Character` over Unicode 17.0.0, the version of the `String` case tables, in the library's snake_case names. It has the two shapes Java has:

- **Module functions on code points.** Each takes a code point as a `uint32`, as Java's static methods take an `int`, and is total over `uint32`: a value above 0x10FFFF has the properties of an unassigned code point, and only the members that build text, UTF-8, or UTF-16 trap for it. The surrogate code points U+D800 through U+DFFF have the category `Surrogate`. There is still no `char` type ([section 8.3](Stdlib-Design#83-scalars)).
- **The class `Character`**, an `#Immutable` value holding one Unicode scalar value, the get-only field `code_point`. `of(code_point)` and `new Character(code_point)` trap with `invalid argument: not a Unicode scalar value` for a surrogate or a value above 0x10FFFF, because a `Character`'s text must be one character; `try_of` returns `None` instead, and `from_text(text)` gives the character of a string that holds exactly one scalar value. `equals` compares the code points, `hash_code` is the code point (Java's `hashCode` is the `char` value, and a `uint32` hashes to itself, [section 5.2](Stdlib-Design#52-equality-hashing-and-text)), `compare_to` orders by code point, and `to_string` is the character. Its methods apply the common module functions to the value: the classification predicates through `is_mirrored`, `get_type`, `get_directionality`, the case mappings (which return `Character`), `digit`, `numeric_value`, `char_count`, and `utf8_length`. The identifier, emoji, and `bidi_class` functions have no method; call them with `code_point`.

| Member | Behavior | Java |
| --- | --- | --- |
| `MinCodePoint`, `MaxCodePoint`, `MinSupplementaryCodePoint`, `Min`/`MaxHighSurrogate`, `Min`/`MaxLowSurrogate`, `Min`/`MaxSurrogate`, `MinRadix`, `MaxRadix`, `UnicodeVersion` | 0, 0x10FFFF, 0x10000, the surrogate bounds, 2, 36, and `"17.0.0"`. | The constants of the same names |
| `get_type(cp) → GeneralCategory`, `category_code(c)` | The General_Category, an enum in Unicode's order (`UppercaseLetter` … `Unassigned`), and its two-letter code. | `getType` |
| `get_directionality(cp) → BidiClass` | The Bidi_Class of a defined code point; `Undefined` when the category is `Unassigned`, including noncharacters. | `getDirectionality` |
| `bidi_class(cp)`, `bidi_code(b)` | The Bidi_Class with the UCD's defaults for unassigned code points (`RightToLeft` in the Hebrew block, `BoundaryNeutral` for noncharacters), and its abbreviation. | |
| `is_upper`, `is_lower` | The Uppercase and Lowercase properties: category Lu or Ll, or Other_Uppercase or Other_Lowercase. | `isUpperCase`, `isLowerCase` |
| `is_title_case`, `is_letter`, `is_digit`, `is_letter_or_digit` | Category Lt; L; Nd; L or Nd. | `isTitleCase`, `isLetter`, `isDigit`, `isLetterOrDigit` |
| `is_alphabetic`, `is_ideographic`, `is_mirrored` | The Alphabetic, Ideographic, and Bidi_Mirrored properties. | `isAlphabetic`, `isIdeographic`, `isMirrored` |
| `is_alpha`, `is_numeric`, `is_alpha_numeric` | A letter; a code point with a Unicode Numeric_Value (any Numeric_Type but None, including fractions, Roman numerals, and CJK ideographs with Unihan values); either. Python's `str.isalpha`, `isnumeric`, and `isalnum` for one character. | |
| `is_whitespace` | Java's set: category Zs, Zl, or Zp except U+00A0, U+2007, and U+202F, and U+0009 through U+000D and U+001C through U+001F. The `String` companion's strip and indentation use it. | `isWhitespace` |
| `is_space_char` | Category Zs, Zl, or Zp. | `isSpaceChar` |
| `is_space` | The White_Space property: U+0009 through U+000D, U+0020, U+0085, U+00A0, and the separators. | Not Java's deprecated `isSpace`, which tests five ASCII characters |
| `is_iso_control`, `is_defined` | U+0000 through U+001F and U+007F through U+009F; a category other than `Unassigned` (surrogates and private use are defined, noncharacters are not). | `isISOControl`, `isDefined` |
| `is_identifier_start`, `is_identifier_part`, `is_identifier_ignorable` | Java's profile of UAX #31: ID_Start and U+2E2F; ID_Continue, U+2E2F, and the ignorable set; U+0000 through U+0008, U+000E through U+001B, U+007F through U+009F, and category Cf. | `isUnicodeIdentifierStart`, `isUnicodeIdentifierPart`, `isIdentifierIgnorable` |
| `is_java_identifier_start`, `is_java_identifier_part` | A letter or category Nl, Sc, or Pc; a letter, category Sc, Pc, Nd, Nl, Mc, or Mn, or the ignorable set. | `isJavaIdentifierStart`, `isJavaIdentifierPart` |
| `is_emoji`, `is_emoji_presentation`, `is_emoji_modifier`, `is_emoji_modifier_base`, `is_emoji_component`, `is_extended_pictographic` | The UTS #51 properties. | `isEmoji` … `isExtendedPictographic` |
| `to_upper`, `to_lower`, `to_title` `→ uint32` | The simple one-to-one mappings of UnicodeData.txt; the code point itself when there is none. The titlecase mapping is the uppercase one where UnicodeData.txt gives none. | `toUpperCase`, `toLowerCase`, `toTitleCase` |
| `to_upper_string(cp)`, `to_lower_string(cp)`, each also with a language tag | The full mappings of `String.to_upper_case` and `to_lower_case` ([String](StandardLibrary#unicode-case-and-locale)): U+00DF gives `"SS"`, U+0130 gives `i` and U+0307, and `"tr"` maps `i` to U+0130. A lone U+03A3 has no cased letter before it and maps to U+03C3. | `String.toUpperCase` on one character |
| `digit(cp, radix) → int` | The decimal digit value of a category Nd code point, or 10 through 35 for A through Z and a through z in ASCII or fullwidth form, when below the radix; otherwise -1. | `digit`; Java returns -1 for a bad radix, this traps |
| `for_digit(value, radix) → uint32` | The lowercase ASCII digit. | `forDigit`; Java returns U+0000 for bad input, this traps |
| `numeric_value(cp) → int` | 10 through 35 for the Latin letters as in `digit`; the Numeric_Value when it is an integer from 0 through 2^31 - 1; -2 for a fraction, a negative value, or a larger value; -1 for none. | `getNumericValue`, over the full Numeric_Value property |
| `numeric_value_text(cp) → Optional<string>` | The exact Numeric_Value, such as `"1/2"`, `"-1/2"`, or `"1000000000000"`. | |
| `is_valid_code_point`, `is_scalar_value`, `is_bmp`, `is_supplementary` | At most 0x10FFFF; and not a surrogate; below 0x10000; 0x10000 through 0x10FFFF. | `isValidCodePoint`, `isBmpCodePoint`, `isSupplementaryCodePoint` |
| `is_surrogate`, `is_high_surrogate`, `is_low_surrogate`, `is_surrogate_pair(high, low)` | Range tests on a `uint32` unit. | The methods of the same names |
| `to_code_point(high, low)`, `high_surrogate(cp)`, `low_surrogate(cp)` | Surrogate pair conversion. A pair that is not one, or a code point that is not supplementary, traps. | `toCodePoint`, `highSurrogate`, `lowSurrogate`, which do not validate |
| `char_count(cp)`, `to_chars(cp) → uint16[list]` | The UTF-16 units: one below 0x10000, where a surrogate code point gives itself, and a pair above. `char_count` does not validate; `to_chars` traps above 0x10FFFF. | `charCount`, `toChars` |
| `code_point_at(units, i)`, `code_point_before(units, i)`, `code_point_count(units, start, end)`, `offset_by_code_points(units, i, n)` | The `uint16[list]` forms. An unpaired surrogate counts as one code point. A range or a move outside the list traps. | The `char[]` forms |
| `utf8_length(cp)`, `utf16_length(cp)`, `to_utf8(cp)` | Byte and unit counts and the UTF-8 bytes of a scalar value. | |
| `to_string(cp)`, `hash(cp)`, `compare(a, b) → Ordering`, `reverse_bytes(unit: uint16)` | The one-character string (traps unless a scalar value), the code point, numeric order, and the unit with its bytes swapped. | `toString(int)`, `hashCode(char)`, `compare`, `reverseBytes` |
| `get_name(cp)`, `code_point_of(name)` | Stubs: they trap with `unsupported: Std.Core.Character.get_name is not implemented in Neura 0.3.0 (the Unicode name table is not bundled)` and the same for `code_point_of`. The name table is about 0.9 MB of text for 35,000 ranges. | `getName`, `codePointOf` |

**Traps.** Each message follows `invalid argument: `: `not a Unicode scalar value` (`of`, `new Character`, `to_string`, `to_upper_string`, `to_lower_string`, `utf8_length`, `utf16_length`, `to_utf8`), `not a Unicode code point` (`to_chars`), `not a surrogate pair` (`to_code_point`), `not a supplementary code point` (`high_surrogate`, `low_surrogate`), `radix outside 2 through 36` (`digit`, `for_digit`), `digit out of range` (`for_digit`), `range outside the list` (`code_point_count`), `offset outside the list` (`offset_by_code_points`), and `NoBlock has no range` (`UnicodeBlock.first` and `last`). A UTF-16 index outside the list in `code_point_at` and `code_point_before` is the list read's own trap.

**Not provided.** Java members that have no counterpart, and why: `MIN_VALUE`, `MAX_VALUE`, `SIZE`, `BYTES`, `charValue`, `valueOf(char)`, and `hashCode(char)` concern the 16-bit `char`, which Neura does not have (`code_point`, `of`, and `hash` take their place); the deprecated `isJavaLetter`, `isJavaLetterOrDigit`, and `isSpace`; `toChars(int, char[], int)` and the `char[]` overloads with an offset, count, start, or limit, since a list slice serves; `codePointAt(CharSequence, int)` and its relatives, which are `String.code_point_at` and its relatives on strings; `describeConstable`; and the deprecated constructor.

`Std.Core.UnicodeScript` and `Std.Core.UnicodeBlock` are Java's `Character.UnicodeScript` and `Character.UnicodeBlock`. Each exports a generated enum, `UnicodeScript` with one variant per Script value sorted by long name, and `UnicodeBlock` with `NoBlock` and the blocks of Blocks.txt in code point order, and the module functions `of(cp)` (`Unknown` or `NoBlock` for a code point outside every script or block and above 0x10FFFF), `name(v)` (`Old_Italic`, `Latin-1 Supplement`), `for_name(text) → Optional` (long names, codes, and aliases under the loose matching of UAX #44: ASCII case, spaces, underscores, and hyphens are ignored), and `all()`. `UnicodeScript.code(v)` is the ISO 15924 code, and `UnicodeBlock.first(v)` and `last(v)` are a block's bounds, which trap for `NoBlock`.

<!-- example: check -->
```neura
let character = import:Std.Core.Character;
let unicode_script = import:Std.Core.UnicodeScript;

let sigma = character.of(931);
let lower = sigma.to_lower();                        // σ, U+03C3
let category = character.get_type(453);             // GeneralCategory.TitlecaseLetter
let title = character.to_title(454);                 // U+01C5
let sharp_s = character.to_upper_string(223);        // "SS"
let fifty = character.numeric_value(8556);           // 50, ROMAN NUMERAL FIFTY
let script = unicode_script.of(1488);                // UnicodeScript.Hebrew
```

**Data and lookup.** `scripts/generate-unicode-tables.py` writes the tables from the Unicode Character Database 17.0.0 as Perl's `Unicode::UCD` exposes it; it checks the version and the SHA-256 of the data it reads, and `--check` compares the checked-in files. Each table is a string of printable bytes, six bits each: a range table holds the first code point of each range with its value, and a boundary table the boundaries of a property's inversion list. A lookup bisects the table, so it reads about log2(ranges) entries: 4,144 General_Category ranges, 1,268 Bidi_Class ranges, 1,510 case ranges, 2,215 numeric ranges, and between 2 and 1,598 boundaries for each binary property. The tables come to about 88 KB of source; a program that does not call a member does not carry its table.

**Verification.** `tests/stdlib/character` checks every code point from 0 through 0x10FFFF, for each of 38 module functions (every predicate, `get_type`, `get_directionality`, `bidi_class`, the three simple case mappings, `digit` with radix 36, `numeric_value`, and the script and block), against digests that its generator computes per code point from the unpacked UCD data; it adds targeted cases (ASCII, Latin-1, final sigma, dotless i, titlecase digraphs, surrogates, private use, noncharacters, unassigned code points, and the largest code point) and spot checks of `numeric_value_text`, the full-case strings, and `digit` at other radices. This proves the packing and the lookups against the Perl 17.0.0 data. Before writing the test, the generator checks that data against three other sources and stops on any difference it does not list: Python's `unicodedata` (Unicode 15.1.0) for every code point assigned by 15.1, for the category, bidi class, mirroring, decimal and numeric values, `isalpha`, `isnumeric`, `isalnum`, `isupper`, `islower`, and the one-to-one upper, lower, and title mappings (the listed differences are 16.0 and 17.0 changes); Perl's regular expression engine for every code point and the binary properties (Alphabetic, Uppercase, Lowercase, Ideographic, ID_Start, ID_Continue, Bidi_Mirrored, White_Space, the six emoji properties) and the categories L, Lt, Nd, and Cn; and the simple case mappings against the 17.0.0 case tables of `String`, built from the unmodified UCD files. The script and block tables and the Unicode 16.0 and 17.0 additions to the Bidi_Class and numeric tables have no source other than the Perl data. Parity with Java is by definition, taken from the Java SE 25 documentation: no Java runtime is part of the tests.
