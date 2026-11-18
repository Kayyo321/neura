# `Std.Crypto.Pki.Der`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Der` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Der** · [`export const Universal: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L30)
  Tag class of universal tags. Since 0.3.0.
- **Der** · [`export const Application: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L33)
  Tag class of application tags. Since 0.3.0.
- **Der** · [`export const ContextSpecific: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L36)
  Tag class of context-specific tags ([n] in ASN.1). Since 0.3.0.
- **Der** · [`export const Private: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L39)
  Tag class of private tags. Since 0.3.0.
- **Der** · [`export const TagBoolean: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L42)
  Since 0.3.0.
- **Der** · [`export const TagInteger: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L44)
  Since 0.3.0.
- **Der** · [`export const TagBitString: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L46)
  Since 0.3.0.
- **Der** · [`export const TagOctetString: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L48)
  Since 0.3.0.
- **Der** · [`export const TagNull: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L50)
  Since 0.3.0.
- **Der** · [`export const TagOid: int = 6`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L52)
  Since 0.3.0.
- **Der** · [`export const TagEnumerated: int = 10`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L54)
  Since 0.3.0.
- **Der** · [`export const TagUtf8String: int = 12`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L56)
  Since 0.3.0.
- **Der** · [`export const TagSequence: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L58)
  Since 0.3.0.
- **Der** · [`export const TagSet: int = 17`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L60)
  Since 0.3.0.
- **Der** · [`export const TagNumericString: int = 18`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L62)
  Since 0.3.0.
- **Der** · [`export const TagPrintableString: int = 19`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L64)
  Since 0.3.0.
- **Der** · [`export const TagTeletexString: int = 20`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L66)
  Since 0.3.0.
- **Der** · [`export const TagIa5String: int = 22`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L68)
  Since 0.3.0.
- **Der** · [`export const TagUtcTime: int = 23`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L70)
  Since 0.3.0.
- **Der** · [`export const TagGeneralizedTime: int = 24`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L72)
  Since 0.3.0.
- **Der** · [`export const TagVisibleString: int = 26`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L74)
  Since 0.3.0.
- **Der** · [`export const TagUniversalString: int = 28`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L76)
  Since 0.3.0.
- **Der** · [`export const TagBmpString: int = 30`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L78)
  Since 0.3.0.
- **Der** · [`export const DefaultMaxDepth: int = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L82)
  The default limit on nesting: the outermost element is depth 0. Since 0.3.0.
- **Der** · [`export let error_at = func<T>(kind: ParseErrorKind, offset: int, message: string) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L86)
  A `Parse` error of the kind at the offset. Tagged `#NoDiscard`. Since 0.3.0.
- **Der** · [`export let Element = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L97)
  One DER element: a view of a tag, a length and contents inside an input list. Tagged `#Final`. Since 0.3.0.
- **Der.Element** · [`export last data: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L99)
  The whole input. Do not change it.
- **Der.Element** · [`export last offset: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L101)
  The position of the first identifier byte.
- **Der.Element** · [`export last header_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L103)
  The number of identifier and length bytes.
- **Der.Element** · [`export last length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L105)
  The number of content bytes.
- **Der.Element** · [`export last tag_class: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L107)
  `Universal`, `Application`, `ContextSpecific` or `Private`.
- **Der.Element** · [`export last constructed: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L109)
  Whether the encoding is constructed.
- **Der.Element** · [`export last tag_number: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L111)
  The tag number.
- **Der.Element** · [`export last depth: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L113)
  The nesting depth: 0 for an element that `parse` returned.
- **Der.Element** · [`export last max_depth: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L115)
  The nesting limit that applies below this element.
- **Der.Element** · [`export let init = func(input: uint8[list], at: int, header: int, content_length: int, class_bits: int, is_constructed: bool, number: int, level: int, limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L118)
  Records a checked element; `parse` and `Reader` build elements.
- **Der.Element** · [`export let content_offset = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L132)
  The position of the first content byte.
- **Der.Element** · [`export let end = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L135)
  The position after the last content byte.
- **Der.Element** · [`export let content = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L138)
  A copy of the content bytes.
- **Der.Element** · [`export let encoded = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L141)
  A copy of the whole encoding: identifier, length and content.
- **Der.Element** · [`export let is_universal = func(number: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L144)
  Whether the tag is the universal tag with the number.
- **Der.Element** · [`export let is_context = func(number: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L147)
  Whether the tag is the context-specific tag [number].
- **Der.Element** · [`export let reader = func -> Reader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L150)
  A reader over the contents of a constructed element.
- **Der.Element** · [`export let children = func -> Result<Element[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L154)
  The elements inside a constructed element. Tagged `#NoDiscard`.
- **Der.Element** · [`export let explicit_inner = func -> Result<Element>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L168)
  The single element inside an explicitly tagged element. Tagged `#NoDiscard`.
- **Der.Element** · [`export let boolean = func -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L180)
  The value of a BOOLEAN: one content byte, 0x00 or 0xFF. Tagged `#NoDiscard`.
- **Der.Element** · [`export let integer = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L197)
  The two's-complement content of an INTEGER, checked to be minimal. Tagged `#NoDiscard`.
- **Der.Element** · [`export let integer_content = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L204)
  The two's-complement content of an implicitly tagged INTEGER, checked to be minimal. Tagged `#NoDiscard`.
- **Der.Element** · [`export let is_negative = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L219)
  Whether an INTEGER (already checked) is negative.
- **Der.Element** · [`export let small_integer = func(max: int64) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L224)
  The value of an INTEGER or ENUMERATED that must be from 0 to `max`; a negative value or one above `max` is an `OutOfRange` error. Tagged `#NoDiscard`.
- **Der.Element** · [`export let null = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L252)
  Checks a NULL: no content. Tagged `#NoDiscard`.
- **Der.Element** · [`export let oid = func -> Result<Oid>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L262)
  The value of an OBJECT IDENTIFIER. Tagged `#NoDiscard`.
- **Der.Element** · [`export let bit_string = func -> Result<BitString>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L269)
  The value of a BIT STRING (or of an implicitly tagged one when `implicit` is true). Tagged `#NoDiscard`.
- **Der.Element** · [`export let bit_string_content = func -> Result<BitString>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L276)
  The value of an implicitly tagged BIT STRING. Tagged `#NoDiscard`.
- **Der.Element** · [`export let octet_string = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L296)
  The content of an OCTET STRING. Tagged `#NoDiscard`.
- **Der.Element** · [`export let is_string = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L302)
  Whether the tag is one of the string types `text` reads.
- **Der.Element** · [`export let text = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L311)
  The text of a string type: UTF8String (valid UTF-8), PrintableString, NumericString, IA5String (bytes below 128), VisibleString (32 to 126), TeletexString (read as Latin-1, as most software does), BMPString (UCS-2, no surrogates) or UniversalString (UCS-4). Tagged `#NoDiscard`.
- **Der.Element** · [`export let string_content = func(kind: int) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L320)
  The content read as the universal string type `kind` (for implicit tags such as dNSName). Tagged `#NoDiscard`.
- **Der.Element** · [`export let time = func -> Result<instant.Instant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L374)
  The instant of a UTCTime (`YYMMDDHHMMSSZ`, years 1950 to 2049) or a GeneralizedTime (`YYYYMMDDHHMMSSZ`, no fraction). Tagged `#NoDiscard`.
- **Der.Element** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L441)
  Equal when the encodings are equal.
- **Der.Element** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L457)
- **Der.Element** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L460)
  "Element(class <c>, tag <n>, <length> bytes at <offset>)".
- **Der** · [`export let Reader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L468)
  Reads consecutive DER elements from a range of an input list. Tagged `#Final`. Since 0.3.0.
- **Der.Reader** · [`export let position: int { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L471)
  The position of the next element.
- **Der.Reader** · [`export last limit: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L475)
  The end of the range.
- **Der.Reader** · [`export let init = func(input: uint8[list], start: int, stop: int, level: int, limit_depth: int, inside_checked: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L484)
  A reader over input[start, end) at the nesting depth, with the nesting limit. `checked` says that the range was already checked in full (as inside a parsed element), so constructed elements are not checked again; pass false for any other input.
- **Der.Reader** · [`export let has_more = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L495)
  Whether bytes remain.
- **Der.Reader** · [`export let peek_is = func(class_bits: int, number: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L498)
  Whether the next element has the tag class and number (false at the end).
- **Der.Reader** · [`export let next = func -> Result<Element>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L523)
  The next element, fully checked. Tagged `#NoDiscard`.
- **Der.Reader** · [`export let next_universal = func(number: int, name: string) -> Result<Element>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L531)
  The next element, which must have the universal tag `number`. Tagged `#NoDiscard`.
- **Der.Reader** · [`export let optional_context = func(number: int) -> Result<Optional<Element>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L543)
  The next element if it has the context tag [number], else None. Tagged `#NoDiscard`.
- **Der.Reader** · [`export let optional_universal = func(number: int) -> Result<Optional<Element>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L553)
  The next element if it has the universal tag, else None. Tagged `#NoDiscard`.
- **Der.Reader** · [`export let finish = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L563)
  Fails when bytes remain. Tagged `#NoDiscard`.
- **Der.Reader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L571)
  "Reader(<position>..<limit>)".
- **Der** · [`export let parse = func(input: uint8[list]) -> Result<Element>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L576)
  Parses an input that holds exactly one DER element, with the default nesting limit. Tagged `#NoDiscard`. Since 0.3.0.
- **Der** · [`export let parse_with_depth = func(input: uint8[list], max_depth: int) -> Result<Element>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L580)
  Parses an input that holds exactly one DER element; nesting deeper than `max_depth` is an error. Tagged `#NoDiscard`. Since 0.3.0.
- **Der** · [`export let copy_range = func(input: uint8[list], start: int, stop: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L755)
  A copy of input[start, end). Tagged `#NoDiscard`. Since 0.3.0.
- **Der** · [`export let hash_range = func(input: uint8[list], start: int, stop: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/der.nra#L765)
  A hash of input[start, end). Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
