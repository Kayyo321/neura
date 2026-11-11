# `Std.Core.String`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.String`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.String`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)
- [String, Unicode case, and character encoding behavior](StandardLibrary#string)
- [Console and string formatting grammar](StandardLibrary#console-formatting)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **String** · [`export let Charset = charsets.Charset`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L17)
  Aliases the supported character encodings from Std.Text.Charsets.
- **String** · [`export let concat = func(s: string, suffix: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L21)
  Returns the text with suffix appended. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let substring = func(s: string, start: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L25)
  Returns the suffix beginning at a UTF-8 byte boundary. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let sub_sequence = func(s: string, start: int, end: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L31)
  Returns the half-open byte range as an immutable string. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let content_equals = func(s: string, other: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L35)
  Tests for identical UTF-8 bytes. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let compare_to = func(s: string, other: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L42)
  Compares UTF-8 bytes lexicographically and returns -1, 0, or 1. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let compare_to_ignore_ascii_case = func(s: string, other: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L46)
  Compares UTF-8 bytes lexicographically after folding ASCII letters. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let to_lower_case = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L51)
  Applies Unicode lowercase mapping with the root locale rules. Tagged `#NoDiscard`, `#ThreadSafe`.
- **String** · [`export let to_upper_case = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L58)
  Applies Unicode uppercase mapping with the root locale rules. Tagged `#NoDiscard`, `#ThreadSafe`.
- **String** · [`export let compare_to_ignore_case = func(s: string, other: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L65)
  Compares scalars after simple uppercase followed by simple lowercase mapping. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let equals_ignore_case = func(s: string, other: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L70)
  Tests equality using simple scalar case mapping. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let case_fold = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L75)
  Applies full locale-independent Unicode case folding, which can expand a scalar. Tagged `#NoDiscard`, `#ThreadSafe`.
- **String** · [`export let compare_to_fold_case = func(s: string, other: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L79)
  Orders the results of full Unicode case folding. Tagged `#ThreadSafe`.
- **String** · [`export let equals_fold_case = func(s: string, other: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L84)
  Tests equality of the results of full Unicode case folding. Tagged `#ThreadSafe`.
- **String** · [`export let starts_with_at = func(s: string, prefix: string, offset: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L89)
  Tests whether the prefix begins at the supplied byte offset. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let region_matches = func(s: string, start: int, other: string, other_start: int, byte_count: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L94)
  Compares two equally sized UTF-8 byte ranges. Invalid regions return false. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let region_matches_ignore_ascii_case = func(s: string, start: int, other: string, other_start: int, byte_count: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L103)
  Compares valid byte ranges after folding ASCII letters. Invalid regions return false.
- **String** · [`export let region_matches_ignore_case = func(s: string, start: int, other: string, other_start: int, byte_count: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L114)
  Compares valid byte ranges using simple Unicode case mapping. Invalid regions return false.
- **String** · [`export let index_of_range = func(s: string, needle: string, start: int, end: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L125)
  Finds the first occurrence wholly within the half-open byte range, or returns -1. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let index_of_code_point = func(s: string, scalar: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L132)
  Finds the first byte offset of the scalar, or returns -1. Tagged `#ThreadSafe`.
- **String** · [`export let last_index_of_from = func(s: string, needle: string, from: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L137)
  Finds the last occurrence starting at or before the specified byte offset, or returns -1. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let last_index_of_code_point = func(s: string, scalar: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L153)
  Finds the last byte offset of the scalar, or returns -1. Tagged `#ThreadSafe`.
- **String** · [`export let code_point_at = func(s: string, offset: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L158)
  Returns the Unicode scalar beginning at a UTF-8 byte offset. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let code_point_before = func(s: string, offset: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L162)
  Returns the Unicode scalar ending at the supplied byte boundary. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let code_point_count = func(s: string, start: int, end: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L167)
  Counts Unicode scalars in a half-open UTF-8 byte range. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let code_points = func(s: string) -> uint32[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L172)
  Copies all Unicode scalars into a list in order. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let get_bytes = func(s: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L175)
  Copies the UTF-8 bytes into a new list.
- **String** · [`export let try_get_bytes = func(s: string, charset: Charset) -> result.Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L185)
  Encodes strictly and reports the byte offset of an unrepresentable scalar. Tagged `#NoDiscard`, `#ThreadSafe`.
- **String** · [`export let decode_bytes = func(bytes: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L195)
  Decodes UTF-8, substituting U+FFFD for each maximal malformed subpart.
- **String** · [`export let try_decode_bytes = func(bytes: uint8[list]) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L204)
  Decodes UTF-8 strictly, returning an error at the first invalid sequence. Tagged `#NoDiscard`, `#ThreadSafe`.
- **String** · [`export let utf16_length = func(s: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L218)
  Explicit UTF-16 adapters. Other positional operations use UTF-8. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let char_at = func(s: string, index: int) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L233)
  Returns a UTF-16 code unit by index, including either half of a surrogate pair. Invalid indexes trap. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let to_char_array = func(s: string) -> uint16[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L253)
  Copies the text into UTF-16 code units in a new list. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let chars = func(s: string) -> uint16[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L257)
  Returns the UTF-16 code units as a list. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let get_chars = func(s: string, start: int, end: int, destination: uint16[], destination_start: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L260)
  Copies a half-open UTF-16 code-unit range into an existing array or list. Invalid ranges trap.
- **String** · [`export let from_char_array = func(units: uint16[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L275)
  Decodes UTF-16 code units. Unpaired surrogates trap. Tagged `#ThreadSafe`.
- **String** · [`export let offset_by_code_points = func(s: string, offset: int, count: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L300)
  Moves a byte offset by a signed number of Unicode scalars. Invalid offsets or positions trap. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let.override trim = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L323)
  Removes leading and trailing code points through U+0020. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **String** · [`export let is_blank = func(s: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L334)
  Tests whether the entire string consists of whitespace, including the empty string. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let strip = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L338)
  Removes whitespace from both ends using the companion's Unicode whitespace set. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **String** · [`export let strip_leading = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L342)
  Removes leading whitespace using the companion's Unicode whitespace set. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **String** · [`export let strip_trailing = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L347)
  Removes trailing whitespace using the companion's Unicode whitespace set. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **String** · [`export let.override lines = func(s: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L361)
  CR, LF, and CRLF each end a line. An empty string has no lines. Tagged `#Pure`, `#ThreadSafe`.
- **String** · [`export let indent = func(s: string, count: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L386)
  Positive counts add spaces. Negative counts remove up to that many leading whitespace code points. Each resulting line ends with LF. Tagged `#NoDiscard`, `#ThreadSafe`.
- **String** · [`export let strip_indent = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L405)
  Removes incidental indentation and trailing whitespace per line. A terminal line separator fixes the removable indentation at zero. Tagged `#NoDiscard`, `#ThreadSafe`.
- **String** · [`export let matches = func(s: string, pattern: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L439)
  Tests whether a regex matches the whole string. An invalid pattern traps.
- **String** · [`export let replace_code_point = func(s: string, target: uint32, replacement: uint32) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L444)
  Replaces every occurrence of a Unicode scalar with another scalar. Tagged `#NoDiscard`, `#ThreadSafe`.
- **String** · [`export let replace_all = func(s: string, pattern: string, replacement: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L449)
  Replaces all regex matches. An invalid pattern or replacement traps. Tagged `#NoDiscard`.
- **String** · [`export let replace_first_regex = func(s: string, pattern: string, replacement: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L454)
  Replaces the first regex match. An invalid pattern or replacement traps. Tagged `#NoDiscard`.
- **String** · [`export let split_regex = func(s: string, pattern: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L458)
  Splits at regex matches and keeps empty pieces.
- **String** · [`export let split_regex_limit = func(s: string, pattern: string, limit: int) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L462)
  Splits into at most the positive limit of pieces, leaving the remainder in the last one.
- **String** · [`export let split_with_delimiters = func(s: string, pattern: string, limit: int) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L466)
  Splits into at most the positive limit of pieces and interleaves the matched delimiters.
- **String** · [`export let formatted = func(s: string, args: ...) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L470)
  Formats the arguments into text with the console format grammar. Invalid formats trap.
- **String** · [`export let vformatted = func(s: string, args: variadic.Argument[]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L474)
  Formats a prebuilt variadic argument array into text. Invalid formats trap.
- **String** · [`export let value_of = func<T>(value: T) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L478)
  Returns the value's to_string representation.
- **String** · [`export let transform = func<T>(s: string, operation: func(string) -> T) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L481)
  Applies the supplied callback to the text and returns its result.
- **String** · [`export let translate_escapes = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string.nra#L486)
  Decodes supported backslash and octal escapes and removes escaped line endings. Malformed escapes trap. Tagged `#NoDiscard`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Current text and formatting contracts

### String

`Std.Core.String` is the companion module for the immutable `string` primitive. It inherits the receiver-first functions of [`Std.Text.Strings`](https://github.com/Kayyo321/neura/wiki/Stdlib-Design#81-strings), including `contains`, `repeat`, UTF-8 construction, and ASCII case mapping. Import the module to call those functions explicitly or use the [primitive member syntax](https://github.com/Kayyo321/neura/wiki/Types#primitive-companions) on a string value.

Text positions and range lengths count UTF-8 bytes except in the explicit UTF-16 adapters. Use `byte_length` for bytes, `scalar_count` for Unicode scalar values, and `utf16_length` for UTF-16 code units. A slice includes its start and excludes its end. Byte slice endpoints must be on UTF-8 boundaries.

| Member | Behavior |
| --- | --- |
| `concat(s, suffix)`, `substring(s, start)`, `substring(s, start, end)`, `sub_sequence(s, start, end)` | Concatenation and checked UTF-8 byte slices. The one-offset form takes the rest of the string. |
| `content_equals(s, other)` | Compare contents against a string or a `StringBuilder`. A nil builder compares unequal. |
| `compare_to(s, other)`, `compare_to_ignore_ascii_case(s, other)` | Bytewise lexicographic comparison as -1, 0, or 1. The second form first folds ASCII letters to lowercase. |
| `starts_with_at(s, prefix, offset)`, `region_matches(s, start, other, other_start, byte_count)`, `region_matches_ignore_ascii_case(...)`, `region_matches_ignore_case(...)` | Compare regions and return false if an offset, length, or boundary is invalid. The last form uses the Unicode comparison described below. |
| `index_of_range(s, needle, start, end)`, `last_index_of_from(s, needle, from)` | Return a byte offset or -1. The first searches inside the checked range. The second searches backward from a valid UTF-8 boundary, including that boundary. |
| `index_of_code_point(s, scalar)`, `last_index_of_code_point(s, scalar)`, `replace_code_point(s, target, replacement)` | Search for or replace a Unicode scalar. Invalid scalar values trap. |
| `code_point_at(s, offset)`, `code_point_before(s, offset)`, `code_point_count(s, start, end)`, `offset_by_code_points(s, offset, count)` | Read or count Unicode scalars, or move a byte offset by a signed number of scalars. Invalid boundaries and moves outside the text trap. |
| `code_points(s)`, `get_bytes(s)` | Copy the Unicode scalars to a `uint32[list]` or the UTF-8 bytes to a `uint8[list]`. |
| `utf16_length(s)`, `char_at(s, index)`, `chars(s)`, `to_char_array(s)` | Count UTF-16 code units, read one code unit as `uint16`, or copy the units to a new `uint16[list]`. The two halves of a supplementary scalar can be read separately. Invalid indexes trap. |
| `get_chars(s, start, end, destination, destination_start)`, `from_char_array(units)` | Copy a UTF-16 code-unit range into an existing `uint16[]` or `uint16[list]`, or decode a `uint16[list]` into a string. Invalid ranges and unpaired surrogates trap. |
| `trim(s)`, `strip(s)`, `strip_leading(s)`, `strip_trailing(s)`, `is_blank(s)` | `trim` removes values through U+0020 at either end. The other operations use the whitespace definition below. An empty string is blank. |
| `lines(s)` | Split at CR, LF, or CRLF. The empty string gives no lines. A terminal separator contributes no extra line. |
| `indent(s, count)`, `strip_indent(s)` | `indent` adds spaces for positive counts or removes up to the magnitude in leading whitespace scalars for negative counts, then ends each line with LF. `strip_indent` removes the indentation common to nonblank lines and the final line, strips trailing whitespace from each line, and normalizes separators to LF. A terminal separator fixes the common indentation at zero. |
| `matches(s, pattern)`, `replace_all(s, pattern, replacement)`, `replace_first_regex(s, pattern, replacement)` | Compile with [`Std.Text.Regex`](https://github.com/Kayyo321/neura/wiki/Stdlib-Design#84-regex). `matches` requires a whole-string match. An invalid pattern or replacement traps. |
| `split_regex(s, pattern)`, `split_regex_limit(s, pattern, limit)`, `split_with_delimiters(s, pattern, limit)` | Split at regular-expression matches. Limits must be positive and count pieces before interleaving matched delimiters. The last piece holds the remainder, and empty pieces are kept. |
| `formatted(s, args...)`, `vformatted(s, args)` | Use the [console format grammar](#console-formatting) and return the result as text. The second accepts a prebuilt `Argument[]`. Invalid formats trap. Neither writes to a stream. |
| `value_of<T>(value)`, `transform<T>(s, operation)` | Format a value that supports `to_string` or apply a callback of type `func(string) -> T`. |
| `translate_escapes(s)` | Translate escaped quotes, backslash, space, and the `b`, `f`, `n`, `r`, and `t` escapes. Also accepts octal escapes through `377` and removes backslash followed by CR, LF, or CRLF. Unknown or incomplete escapes trap. |

The whitespace set follows Java's `Character.isWhitespace` for strip and indentation. It includes U+0009–000D, U+001C–0020, U+1680, U+2000–2006, U+2008–200A, U+2028, U+2029, U+205F, and U+3000. The original `Std.Text.Strings.trim` and `Std.Text.Strings.lines` retain their own behavior. [Java SE 25 Character](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/lang/Character.html#isWhitespace(int))

Neura strings always contain valid UTF-8. Hashes, ordinary comparisons, and byte indexes use that representation. The explicit UTF-16 adapters work in code units. Regular-expression and formatting operations use Neura's syntax. `split` and `replace` from `Std.Text.Strings` use literal matching. JVM object interning and constant descriptors have no direct equivalent. [Java SE 26 String](https://docs.oracle.com/en/java/javase/26/docs/api/java.base/java/lang/String.html)

#### Unicode case and locale

The casing tables are pinned to Unicode 17.0.0. The no-locale forms of `to_lower_case` and `to_upper_case` use Unicode's root rules, including full mappings such as `"Straße".to_upper_case()` yielding `"STRASSE"` and the contextual Greek final sigma. The overloads accepting a language tag recognize the primary language `tr`, `az`, or `lt` without regard to ASCII case. These select the Turkish, Azerbaijani, and Lithuanian rules from Unicode SpecialCasing. Other tags select the root rules. Case conversion has no dependency on the machine's default locale. Locale-sensitive sorting and formatting are separate concerns. [Unicode casing FAQ](https://www.unicode.org/faq/casemap_charprop.html), [Unicode 17.0 SpecialCasing](https://www.unicode.org/Public/17.0.0/ucd/SpecialCasing.txt)

`equals_ignore_case` and `compare_to_ignore_case` use the simple lowercase of the simple uppercase of each scalar. The comparison orders those scalars lexicographically and returns -1, 0, or 1. `region_matches_ignore_case` applies the same rule to the two selected UTF-8 byte ranges.

`case_fold` uses the full, locale-independent Unicode mapping, which can expand a scalar to several scalars. `equals_fold_case` and `compare_to_fold_case` compare the resulting text. For example, `"Fuß".equals_fold_case("FUSS")` is true and `"Fuß".equals_ignore_case("FUSS")` is false. Normalization is a separate operation. [Unicode 17.0 CaseFolding](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt)

#### Character encodings

`String.Charset` aliases the `Std.Text.Charsets.Charset` enum. It covers Java's standard encodings.

| Enum value | Name |
| --- | --- |
| `Ascii` | `US-ASCII` |
| `Latin1` | `ISO-8859-1` |
| `Utf8` | `UTF-8` |
| `Utf16`, `Utf16Be`, `Utf16Le` | `UTF-16`, `UTF-16BE`, `UTF-16LE` |
| `Utf32`, `Utf32Be`, `Utf32Le` | `UTF-32`, `UTF-32BE`, `UTF-32LE` |

`value.get_bytes()` returns a UTF-8 copy. Pass a charset value or a case-insensitive name to select another encoding. `string.decode_bytes(bytes)` decodes a `uint8[list]` as UTF-8. Its two-argument forms take a charset value or name. Common aliases include `ascii`, `latin1`, `utf8`, and the UTF-16 and UTF-32 names without hyphens. `Std.Text.Charsets.for_name` returns an error for an unsupported name. `get_bytes` and `decode_bytes` trap on an unsupported name.

When encoding to ASCII or Latin-1, an unrepresentable scalar becomes `?`. Decoding replaces each malformed unit or truncated tail with U+FFFD, using Unicode maximal subparts for UTF-8. `try_get_bytes` and `try_decode_bytes` return `Result` errors instead of replacing or trapping. An encoding error points to the first invalid byte offset in the input text or bytes. The inherited `from_bytes` remains the strict UTF-8 decoder.

The byte-order-aware `Utf16` and `Utf32` variants encode a big-endian byte-order mark at the start and read either byte order when a mark is present. They decode unmarked input as big-endian. The explicit-endian variants omit a mark when encoding and preserve a decoded U+FEFF as text. Charset conversion always returns ordinary UTF-8 text on decode. [Java SE 26 Charset](https://docs.oracle.com/en/java/javase/26/docs/api/java.base/java/nio/charset/Charset.html)

```neura
let lower = "I".to_lower_case("tr");              // ı
let bytes = lower.get_bytes("UTF-16LE");
let decoded = string.decode_bytes(bytes, "UTF-16LE");
```

### Console formatting

`Console.format(pattern, args...)` and the static `Console.Formatter.format` return a UTF-8 string. `print` and `println` format to stdout, and `eprint` and `eprintln` format to stderr. The two newline forms append one LF. The module functions have `try_` counterparts that return a `Result` containing the string or `Unit`. For an existing `Std.Core.Variadic.Argument[]`, use `vformat` or `try_vformat`. Use `write`, `print_line`, or `error_line` for already-formatted text that may contain literal braces.

```neura
let console = import:Std.Io.Console;

console.println("{}, {}", "hello", "Neura");           // hello, Neura
console.println("{{{1}}}: {0:#06x}", 42, "value");     // {value}: 0x002a
let label = console.format("{:*^7} {:+.2f}", "ok", 1.25); // **ok*** +1.25
```

`{}` uses the next argument, and `{0}`, `{1}`, and so on use zero-based indexes. A pattern uses one indexing mode throughout. Explicit indexes may repeat and unused arguments are permitted. Write `{{` and `}}` for single literal braces. A colon in a field introduces its format specification.

| Option | Behavior |
| --- | --- |
| Fill and alignment | `<` left, `>` right, `^` center. An optional Unicode scalar before the alignment symbol replaces the default space. Text defaults to left alignment and numbers to right alignment. |
| Numeric sign and padding | `+` forces a sign, `-` is the default, and a space reserves the positive sign. `#` adds a radix prefix or forces a decimal point. `0` pads after the sign and radix prefix when alignment is implicit. |
| Width and precision | A decimal width sets a minimum field size. `.precision` limits text to that many Unicode scalars or controls decimal digits for floats. Width is measured in Unicode scalars. A nested field such as `{:>{}}` or `{0:.{1}f}` takes its width or precision from a nonnegative integer argument. |
| Integer presentation | `d` decimal, `b` or `B` binary, `o` octal, `x` or `X` hexadecimal, and `c` a Unicode scalar. Booleans also support the numeric presentations other than `c`. |
| Float presentation | `f` fixed, `e` scientific, `g` significant-digit formatting, and `a` hexadecimal. Uppercase variants capitalize the result. Decimal presentations default to six digits, while hexadecimal omits trailing zeros by default. All accept precision up to 100. The default presentation uses the argument type's shortest round-trip text when precision is omitted. |
| Text presentation | `s` inserts text. `?` quotes and escapes text. Objects use their `to_string()` method and a nil reference renders as `nil`. Booleans default to `true` and `false`. |

Format strings are checked at runtime and may come from any string expression. Invalid formats return an `Error.Parse` with a zero-based byte offset from the `try_` forms, or trap from the other forms. The library finishes formatting before sending anything to a stream, so a format error produces no partial output. Output uses the same host services and capability as raw console writes.

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 8.1 `Strings`

All of `Strings` needs T1. String operations are functions that take the string first. All positions are byte offsets into the UTF-8 encoding, of type `int`. No member is named `length`: `byte_length` and `scalar_count` say which unit they count. An offset that is out of range, or not on a character boundary where one is required, traps.

| Member | Behavior |
| --- | --- |
| `byte_length(s) → int` | Constant time. |
| `scalar_count(s) → int` | Counts bytes that are not continuation bytes. Linear. |
| `is_empty(s) → bool` | |
| `byte_at(s, offset) → uint8` | |
| `scalar_at(s, offset) → uint32` | Decodes the character starting at `offset`. |
| `is_boundary(s, offset) → bool` | True for 0, for the byte length, and for any offset whose byte is not a continuation byte. False out of range. |
| `scalars(s)` | A built-in list of `uint32`: the scalar values in order. For-each works on it directly. |
| `scalar_offsets(s)` | A built-in list of `[int, uint32]`: each starting offset with its scalar value. |
| `slice(s, start, end) → string`, `slice_from(s, start) → string` | Both ends on boundaries, `start` not above `end`. |
| `equals(a, b) → bool`, `hash(s) → uint32`, `compare(a, b) → Ordering` | [Sections 5.2](Stdlib-Design#52-equality-hashing-and-text) and [5.3](Stdlib-Design#53-ordering). |
| `starts_with(s, prefix)`, `ends_with(s, suffix)`, `contains(s, needle)` `→ bool` | The empty string is a prefix, suffix, and substring of every string. |
| `index_of(s, needle) → int`, `index_of_from(s, needle, from) → int` | Offset of the first occurrence at or after `from`, or -1. An empty needle matches at `from`. |
| `last_index_of(s, needle) → int` | Offset of the last occurrence, or -1. |
| `split(s, separator)` | A built-in list of strings: every piece between occurrences, left to right without overlap. Empty pieces are kept, so `a,,b,` gives four pieces and the empty string gives one. An empty separator traps. |
| `split_limit(s, separator, limit)` | At most `limit` pieces; the last holds the remainder. `limit` below 1 traps. |
| `lines(s)` | Splits at LF; a CR immediately before an LF is removed; a final empty piece after a trailing LF is dropped. |
| `join(parts: string[list], separator) → string` | |
| `repeat(s, count) → string` | A negative count traps. |
| `trim(s)`, `trim_start(s)`, `trim_end(s)` | Remove space, tab, LF, vertical tab, form feed, and CR. |
| `strip_prefix(s, prefix)`, `strip_suffix(s, suffix)` `→ Optional<string>` | The remainder when the affix is present. |
| `replace(s, target, replacement)`, `replace_first(...)` | Every non-overlapping occurrence, left to right. An empty target traps. |
| `to_ascii_upper(s)`, `to_ascii_lower(s)`, `equals_ignore_ascii_case(a, b)` | Change or ignore only the letters A through Z and a through z. |
| `pad_start(s, width, pad: string)`, `pad_end(...)` | Pads to `width` scalar values with copies of `pad`, which must be one scalar value. |
| `reverse(s) → string` | Reverses the order of scalar values. |
| `to_bytes(s)` | A built-in list of `uint8`, a copy. |
| `from_bytes(bytes: uint8[list]) → Result<string>` | `Encoding` error with the offset of the first invalid byte. |
| `from_bytes_lossy(bytes: uint8[list]) → string` | Each maximal invalid sequence becomes one U+FFFD. |
| `from_scalars(scalars: uint32[list]) → string` | Traps for a value that is not a Unicode scalar value. |

Functions that return several strings return a built-in list, not an `ArrayList`: the built-in for-each works on it, and `Std.Text` then needs nothing from `Std.Collections`.

**Search algorithm.** The searches, comparisons, trims, splits, `lines`, replacements, ASCII case mappings, `scalar_count`, and `hash` run on the `neura.text`, `neura.hash`, and `neura.encoding` kernels (P2a). The text searches use the two-way string-matching algorithm, so a search is linear in the length of the text it reads plus the needle's length, never their product. A kernel is charged by the work its arguments allow (P2a), so the steps of each member are linear: `index_of` and `index_of_from` search in windows that start at 128 bytes and double, so their steps grow with the distance from `from` to the occurrence rather than with the rest of the text, and a loop that calls `index_of_from` after each occurrence is linear in the text; `split`, `split_limit`, and `lines` find every separator in two kernel calls over the text (one counts, one writes the offsets) and then slice the pieces, so they are linear in the text plus the number of pieces.

**UTF-8 validity** follows the Unicode definition: shortest form only, no surrogates, nothing above 0x10FFFF. The validator, the `neura.encoding.utf8_check` and `utf8_validate` kernels, must agree with the validation NVM performs when constructing text; its Neura original is the oracle of the `strings` and `utf8` tests.

**Unicode scope.** `Std.Text.Strings` handles UTF-8 encoding, decoding, and iteration for all Unicode scalar values. Its original ASCII case and classification methods keep their scope. The [`String` companion](StandardLibrary#string) adds a fixed whitespace set for strip and indentation, Unicode 17.0.0 case conversion and folding, and standard charset conversion from `Std.Text.Charsets`. These are ordinary Neura routines with embedded Unicode data and use no host locale or charset services.

### 13.1 `Std.Encoding`

Everything here needs T1 except the `Json` value type itself.

- **`Utf8`:** `encode(s)` (a built-in `uint8` list); `decode(bytes) → Result<string>`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1). `validate` works on bytes alone. The kernel `neura.encoding.utf8_validate` computes it (P2a).
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → Result<uint8[list]>` (either case; an odd length or a non-hex character fails with an `Encoding` error). The kernels `neura.encoding.hex_encode` and `hex_decode` compute it (P2a).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero. The kernels `neura.encoding.base64_encode` and `base64_decode` compute every form (P2a).
- **`Csv`** (RFC 4180): `parse(text)` returns a `Result` of a built-in list of built-in lists of strings, and `write(rows) → string`. Fields are separated by commas and records by LF or CRLF. A quoted field may contain commas, line breaks, and doubled quotes. A quote inside an unquoted field, or text after a closing quote, fails with a `Parse` error. A final line break does not add an empty record. The writer quotes a field only when it contains a comma, a quote, a CR, or an LF, and ends each record with LF.

**`Json`** (RFC 8259).

**`JsonValue` is a recursive payload enum.** It is read with `match` and walked with mutually recursive functions.

| Variant | Payload |
| --- | --- |
| `Null` | None |
| `Boolean` | `bool` |
| `Number` | `string`: the number's JSON text |
| `Text` | `string` |
| `Array` | A built-in list of `JsonValue` |
| `Object` | A built-in list of `[string, JsonValue]`, in member order |

- **Objects are member lists.** They keep insertion order in the serialized representation. Lookup is a linear scan. `Json.index(members)` builds a `StringMap<JsonValue>` when callers need repeated lookups.
- **Numbers keep their text**, so no precision is lost in a round trip. The payload must be valid JSON number text; the writer checks it and traps with `invalid argument: number text` otherwise.
- **Aliasing.** The payloads of `Array` and `Object` are built-in lists: they are mutable, and the same list can be reached from more than one value. A `JsonValue` is therefore not a frozen tree. Changing a list changes every value that holds it, and a list can be made to contain a value that holds the same list. `write`, `equals`, and `hash` do not detect such a cycle and end with the frame-limit fault. The parser always produces a tree with no sharing.

The module `Json`:

| Function | Behavior | Needs |
| --- | --- | --- |
| `number_of_int64(v: int64) → JsonValue` | A `Number` with the integer's text. | None |
| `number_of_float64(v: float64) → JsonValue` | A `Number` with the float's text. NaN and infinities trap. | N1 |
| `member(members, name: string) → Optional<JsonValue>` | The first member with that name. | T1 |
| `number_to_int64(text: string) → Optional<int64>` | `Some` when the text has no fraction or exponent and fits. | T1 |
| `number_to_float64(text: string) → float64` | The nearest value. | T1, N1 |
| `number_to_big_decimal(text: string) → BigDecimal` | Exact. | T1 |
| `parse(text: string) → Result<JsonValue>` | Below. | T1 |
| `write(value) → string`, `write_pretty(value, indent: int) → string` | Below. | T1 |
| `equals(a, b) → bool`, `hash(value) → uint32` | Structural. Numbers are equal when their exact decimal values are. Objects are equal when they have the same number of members and each member of one has an equal value under the same name in the other, regardless of order. | T1 |

- **Parsing** is strict recursive descent: no comments, no trailing commas, no leading zeros, only the four JSON whitespace characters. String escapes include `\uXXXX`, with surrogate pairs combined; an unpaired surrogate fails. Nesting deeper than 512 fails. For a repeated member name, the last value wins and the name keeps its first position. Anything but whitespace after the value fails. Errors are `Parse` errors with the byte offset.
- **Writing.** `write` produces no whitespace. `write_pretty` puts each element and member on its own line, indented by `indent` spaces per level, with `: ` after names; empty arrays and objects are `[]` and `{}`. Strings escape `"`, `\`, and characters below 0x20 (using `\b`, `\f`, `\n`, `\r`, `\t`, and otherwise `\u00XX` with lowercase hex). Nothing else is escaped. Every member of an object is written, including repeated names.
