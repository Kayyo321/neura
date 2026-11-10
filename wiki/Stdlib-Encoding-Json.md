# `Std.Encoding.Json`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Encoding.Json`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Encoding.Json`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Json** · [`export let JsonValue = enum ( Null, Boolean(bool), Number(string), Text(string), Array(JsonValue[list]), Object([string, JsonValue][list]) )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L24)
  Represents null, booleans, number text, strings, arrays, and ordered object members.
- **Json** · [`export let number_of_int64 = func(v: int64) -> JsonValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L37)
  A Number with the integer's decimal text.
- **Json** · [`export let number_of_float64 = func(v: float64) -> JsonValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L43)
  A Number with the float's shortest round-trip text (Stdlib.md section 7.5), which is valid JSON number text. NaN and the infinities trap with "invalid argument: number is not finite".
- **Json** · [`export let member = func(members: [string, JsonValue][list], name: string) -> Optional<JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L51)
  The value of the first member with the name.
- **Json** · [`export let number_to_int64 = func(text: string) -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L62)
  Some(the value) when the text is a JSON number with no fraction and no exponent whose value fits in int64; otherwise None, including for text that is not a JSON number.
- **Json** · [`export let number_to_float64 = func(text: string) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L97)
  The float64 nearest the number, ties to even (Float64.parse). Text that is not a JSON number traps with "invalid argument: number text".
- **Json** · [`export let number_to_big_decimal = func(text: string) -> big_decimals.BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L109)
  The exact value: the number's digits as the unscaled value, with the scale of its fraction digits minus its exponent, so "1.50" has scale 2 and "15e-1" scale 1. Text that is not a JSON number traps with "invalid argument: number text"; a number whose scale leaves the int range, such as 1e3000000000, traps with "invalid argument: number exponent out of range".
- **Json** · [`export let parse = func(text: string) -> Result<JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L133)
  The value, or a Parse error. Parsing is strict: no comments, no trailing commas, no leading zeros, and only space, tab, LF, and CR as whitespace. Nesting deeper than 512 arrays and objects fails. For a repeated member name the last value wins and the name keeps its first position. Offsets are byte offsets. The errors: - Empty, "no value": the text is empty or only whitespace (offset: its length). - UnexpectedEnd, "unexpected end of input": at the text's length. - Syntax: "expected a value", "expected ',' or ']'", "expected ',' or '}'", "expected a member name", "expected ':'", "unexpected text after value", at the offending byte. - InvalidCharacter: "invalid literal", "expected a digit", "leading zero", "control character in string" at the offending byte; "invalid escape", "invalid \u escape", and "unpaired surrogate" at the escape's backslash. - OutOfRange, "nesting deeper than 512": at the bracket that opens the 513th level.
- **Json** · [`export let write = func(value: JsonValue) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L148)
  The value's JSON text with no whitespace. A Number whose text is not a JSON number traps with "invalid argument: number text".
- **Json** · [`export let write_pretty = func(value: JsonValue, indent: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L159)
  The value's JSON text with each element and member on its own line, indented by `indent` spaces per level, and ": " after member names. Empty arrays and objects are [] and {}. A negative indent traps with "invalid argument: negative indent".
- **Json** · [`export let equals = func(a: JsonValue, b: JsonValue) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L174)
  Structural equality. Numbers are equal when their exact decimal values are, so 1, 1.0, 10e-1, and -0 versus 0 compare by value. Arrays compare element by element. Objects are equal when they have the same number of members and each member of either has a member with the same name and an equal value in the other, in any order. A Number whose text is not a JSON number traps with "invalid argument: number text".
- **Json** · [`export let hash = func(value: JsonValue) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L224)
  A hash consistent with equals. Null is 1; Boolean(b) is Hash.combine(2, Hash.of_bool(b)); Number is Hash.combine(3, the hash of its canonical text); Text(s) is Hash.combine(4, s.hash_code()); Array starts at 5 and combines each element's hash in order; Object is Hash.combine(Hash.combine(6, member count), S), where S is the wrapping sum of the distinct values among the members' Hash.combine(name hash, value hash). The canonical text of a number is "0" for zero and otherwise an optional "-", the significant digits without leading or trailing zeros, "e", and the decimal exponent that makes them an integer: 120.50 is "1205e-1".
- **Json** · [`export let index = func(members: [string, JsonValue][list]) -> StringMap<JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/encoding/json.nra#L235)
  A StringMap from each member name to its value, in member order. For a repeated name the last value wins and the name keeps its first position, as in parse.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 13.1 `Std.Encoding`

Everything here needs T1 except the `Json` value type itself.

- **`Utf8`:** `encode(s)` (a built-in `uint8` list); `decode(bytes) → Result<string>`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1). `validate` works on bytes alone and can be written today.
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → Result<uint8[list]>` (either case; an odd length or a non-hex character fails with an `Encoding` error).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero.
- **`Csv`** (RFC 4180): `parse(text)` returns a `Result` of a built-in list of built-in lists of strings, and `write(rows) → string`. Fields are separated by commas and records by LF or CRLF. A quoted field may contain commas, line breaks, and doubled quotes. A quote inside an unquoted field, or text after a closing quote, fails with a `Parse` error. A final line break does not add an empty record. The writer quotes a field only when it contains a comma, a quote, a CR, or an LF, and ends each record with LF.

**`Json`** (RFC 8259).

**Recommendation: `JsonValue` is a recursive payload enum.** The first plan had an interface, six implementing classes, a kind enum, and five accessors returning `Optional`. All of that is one declaration and `match`, and it compiles today: the probe declared this shape and walked it with mutually recursive functions in both build modes.

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
| `number_of_int64(v: int64) → JsonValue` | A `Number` with the integer's text. | Today |
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
