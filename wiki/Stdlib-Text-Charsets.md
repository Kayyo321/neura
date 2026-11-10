# `Std.Text.Charsets`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Text.Charsets`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/charsets.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Text.Charsets`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)
- [String, Unicode case, and character encoding behavior](StandardLibrary#string)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Charsets** · [`export let Charset = enum (Ascii, Latin1, Utf8, Utf16, Utf16Be, Utf16Le, Utf32, Utf32Be, Utf32Le)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/charsets.nra#L11)
  Selects ASCII, Latin-1, UTF-8, or a UTF-16 or UTF-32 byte-order policy.
- **Charsets** · [`export let for_name = func(name: string) -> result.Result<Charset>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/charsets.nra#L17)
  Resolves a supported charset name or alias without regard to ASCII case.
- **Charsets** · [`export let encode = func(text: string, charset: Charset) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/charsets.nra#L51)
  Encodes text, substituting a question mark for unrepresentable ASCII or Latin-1 scalars.
- **Charsets** · [`export let try_encode = func(text: string, charset: Charset) -> result.Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/charsets.nra#L55)
  Encodes strictly and returns an Encoding error for the first unrepresentable scalar.
- **Charsets** · [`export let decode = func(bytes: uint8[list], charset: Charset) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/charsets.nra#L59)
  Decodes bytes, substituting U+FFFD for malformed input.
- **Charsets** · [`export let try_decode = func(bytes: uint8[list], charset: Charset) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/charsets.nra#L63)
  Decodes strictly and returns an Encoding error at the first malformed input.
<!-- END SOURCE API -->

## Current text and formatting contracts

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

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 8.1 `Strings`

All of `Strings` needs T1. String operations are functions that take the string first. **Decision:** all positions are byte offsets into the UTF-8 encoding, of type `int`. No member is named `length`: `byte_length` and `scalar_count` say which unit they count. An offset that is out of range, or not on a character boundary where one is required, traps.

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

**Search algorithm.** `index_of` scans for the needle's first byte and then compares the remaining bytes in place. Its worst case is the product of the two lengths. The simple scan has the smallest circuit and a predictable step count.

**UTF-8 validity** follows the Unicode definition: shortest form only, no surrogates, nothing above 0x10FFFF. The validator is Neura code in `Std.Text` and must agree with the validation NVM performs when constructing text.

**Unicode scope. Decision:** `Std.Text.Strings` handles UTF-8 encoding, decoding, and iteration for all Unicode scalar values. Its original ASCII case and classification methods keep their scope. The [`String` companion](https://github.com/Kayyo321/neura/wiki/StandardLibrary#string) adds a fixed whitespace set for strip and indentation, Unicode 17.0.0 case conversion and folding, and standard charset conversion from `Std.Text.Charsets`. These are ordinary Neura routines with embedded Unicode data and use no host locale or charset services.
