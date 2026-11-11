# `Std.Text.Scalars`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Text.Scalars`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Text.Scalars`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Scalars** · [`export const Replacement: uint32 = 65533`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L9)
  U+FFFD REPLACEMENT CHARACTER.
- **Scalars** · [`export let is_valid = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L12)
  At most 0x10FFFF and outside the surrogates 0xD800 through 0xDFFF.
- **Scalars** · [`export let utf8_length = func(scalar: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L17)
  The number of UTF-8 bytes that encode the scalar value, 1 through 4. Traps with "invalid argument: not a Unicode scalar value" for a surrogate or a value above 0x10FFFF.
- **Scalars** · [`export let is_ascii = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L34)
  Tests whether a value is below U+0080.
- **Scalars** · [`export let is_ascii_digit = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L37)
  0 through 9.
- **Scalars** · [`export let is_ascii_hex_digit = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L40)
  0 through 9, A through F, and a through f.
- **Scalars** · [`export let is_ascii_alphabetic = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L44)
  Tests whether the value is an ASCII letter.
- **Scalars** · [`export let is_ascii_alphanumeric = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L48)
  Tests whether the value is an ASCII letter or decimal digit.
- **Scalars** · [`export let is_ascii_uppercase = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L52)
  Tests whether the value is in the ASCII uppercase letter range.
- **Scalars** · [`export let is_ascii_lowercase = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L55)
  Tests whether the value is in the ASCII lowercase letter range.
- **Scalars** · [`export let is_ascii_whitespace = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L59)
  Space, tab, LF, vertical tab, form feed, and CR: the set that Strings.trim removes and that the Regex class \s matches.
- **Scalars** · [`export let is_ascii_punctuation = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L63)
  The 32 graphic ASCII characters that are not letters or digits.
- **Scalars** · [`export let is_ascii_control = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L69)
  U+0000 through U+001F and U+007F.
- **Scalars** · [`export let to_ascii_upper = func(scalar: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L72)
  a through z become A through Z; every other value is returned as is.
- **Scalars** · [`export let to_ascii_lower = func(scalar: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L76)
  A through Z become a through z; every other value is returned as is.
- **Scalars** · [`export let digit_value = func(scalar: uint32, radix: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L83)
  The digit's value in the radix, letters in either case, or -1 when the scalar is not a digit of the radix. A radix outside 2 through 36 traps with "invalid argument: radix outside 2 through 36".
- **Scalars** · [`export let from_digit = func(value: int, radix: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/scalars.nra#L98)
  The lowercase digit for a value from 0 through radix - 1. A radix outside 2 through 36 traps as in digit_value; a value outside the radix traps with "invalid argument: digit out of range".
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 8.3 `Scalars`

**There is no `char` type.** A code point is a `uint32` holding a Unicode scalar value. `StringBuilder.append_scalar` and `Strings.from_scalars` print a code point as its character explicitly.

`Scalars` is a module of functions on `uint32`: `is_valid` (at most 0x10FFFF and not a surrogate), `utf8_length` (1 through 4), `is_ascii`, `is_ascii_digit`, `is_ascii_hex_digit`, `is_ascii_alphabetic`, `is_ascii_alphanumeric`, `is_ascii_uppercase`, `is_ascii_lowercase`, `is_ascii_whitespace`, `is_ascii_punctuation`, `is_ascii_control`, `to_ascii_upper`, `to_ascii_lower`, `digit_value(scalar, radix: int) → int` (-1 when it is not a digit of the radix), and `from_digit(value: int, radix: int) → uint32` (lowercase; traps when out of range). Each is a range comparison; none uses a table. The constant `Replacement` is 0xFFFD.

### 18.4 Text

- **Offsets.** Library offset checks trap with `invalid argument: offset out of range` or `invalid argument: offset is not a character boundary`; `byte_at`, `slice`, and `slice_from` use the language's own traps.
- **Strings.** `lines("")` is `[""]`. `last_index_of(s, "")` is the byte length. `pad_start` and `pad_end` return the text unchanged for a width at or below its scalar count. `from_bytes` reports the offset of the first ill-formed sequence with the message `invalid start byte`, `invalid continuation byte`, or `unexpected end of data`.
- **Scalars.** `utf8_length` traps for a value that is not a scalar value. `is_ascii_whitespace` includes vertical tab, matching `trim` and `\s`.
- **Regex syntax.** `(?<name>…)` and `(?P<name>…)` both name a group. Any ASCII punctuation may be escaped; other escapes are errors. A literal `{` must be escaped. A quantified assertion is `nothing to repeat`, a second quantifier `repeated quantifier`, and `+` after a quantifier `possessive quantifiers are not supported`.
- **Regex matching.** `$` without `multi_line` matches only at the end, and `.` excludes only LF. An iteration beyond a quantifier's minimum that matches empty fails, and a capture inside a repeated group keeps its last value. The Pike machine keeps one thread per instruction per position, so a loop whose body can match empty can merge iterations and differ from every backtracking order; for example `(?:a*?)*` on `aa` matches `[0, 1)`. Replacements validate before matching, accept `${number}`, and trap on a malformed `$`. `group`, `group_start`, and `named_group` trap for an unknown index or name.
