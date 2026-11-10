# `Std.Digest.Crc32`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Digest.Crc32`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/digest/crc32.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Digest.Crc32`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Crc32** · [`export let Crc32 = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/digest/crc32.nra#L56)
  A running checksum. A new one, or one after reset, has the value 0, the checksum of no bytes.
- **Crc32.Crc32** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/digest/crc32.nra#L61)
  Initializes the checksum of an empty byte stream.
- **Crc32.Crc32** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/digest/crc32.nra#L65)
  Adds the bytes to the checksum.
- **Crc32.Crc32** · [`export let value = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/digest/crc32.nra#L71)
  The checksum of every byte added since creation or the last reset. Reading it does not end the computation.
- **Crc32.Crc32** · [`export let reset = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/digest/crc32.nra#L74)
  Restores the initial register and discards the previous checksum state.
- **Crc32** · [`export let create = func -> Crc32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/digest/crc32.nra#L80)
  A new checksum with no bytes added.
- **Crc32** · [`export let of = func(bytes: uint8[list]) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/digest/crc32.nra#L83)
  The checksum of the bytes.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 13.2 `Std.Digest`

Both can be written today: they work on built-in `uint8` lists with the 32-bit recipes of [section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics).

- **`Crc32`:** the IEEE 802.3 checksum (reflected polynomial 0xEDB88320, initial value and final exclusive-or of all ones). A 256-entry `uint32` list is computed on first use. `update(bytes)`, `value() → uint32`, `reset()`, and the module function `of(bytes) → uint32`.
- **`Sha256`:** FIPS 180-4. `update(bytes)`, `finish()` (a built-in list of 32 bytes; a later `update` or `finish` traps with `invalid state: digest finished`), and the module function `of(bytes)`.

These are for integrity checks and content addressing. The library makes no claim about timing behavior.

### 18.7 Encodings and digests

- **Additions.** `Crc32.create()` and `Sha256.create()` make fresh digests; `Base64.decode_unpadded` and `decode_url_unpadded` read the unpadded forms, which reject padding.
- **Utf8** delegates to `Strings`, so its errors match `Strings.from_bytes`; `validate` returns the length of the longest valid prefix, the offset `decode` reports.
- **Hex errors.** `invalid hex digit` at the byte; `odd number of hex digits` at the text length, reported only when every digit is valid.
- **Base64 errors.** `invalid character`, `invalid padding`, and `unexpected padding` at the byte; `unexpected end of data` at the text length; `nonzero trailing bits` at the last character. The first structural error wins.
- **Csv.** A CR that does not start a CRLF is data. An empty line is a record with one empty field, and a record with no fields is written as an empty line. Errors: `quote in unquoted field` and `text after closing quote` (Syntax), `unterminated quoted field` (UnexpectedEnd).
- **Json parse errors.** `Empty` for empty or all-whitespace text; `UnexpectedEnd` at the text length; `Syntax` for structure; `InvalidCharacter` inside a token, with escapes reported at the backslash; `OutOfRange` at the bracket that opens level 513. Top-level scalars are accepted.
- **Json values.** Numbers compare by a canonical decimal text, including exponents beyond `int64`. `hash` starts each variant from its position (Null 1 through Object 6), and an object's hash combines its member count with the wrapping sum of its distinct member hashes, so it agrees with order-insensitive equality even when names repeat. `number_to_int64` is `None` for anything but a plain integer. `index` keeps the last value of a repeated name. A `StringMap<JsonValue>` supports lookups; its `equals` and `to_string` are rejected at build time because `JsonValue` has no built-in value members.
- **Json traps.** `number is not finite`, `negative indent`, `number text`, and `number exponent out of range`, after `invalid argument: `.
- **Sha256** keeps rolled loops: unrolling cut steps by about four but grew the image by 1.5 to 3.6 MB.
