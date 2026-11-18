# `Std.Net.Http.Multipart`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Multipart`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Multipart`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Multipart** · [`export let MultipartEvent = enum (  PartStart(http_headers.Headers),  Data(uint8[list]),  PartEnd,  Done )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L25)
  What the parser found, in order: each part is PartStart, zero or more Data, PartEnd; Done follows the close delimiter.
- **Multipart** · [`export let MultipartLimits = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L39)
  The limits of a multipart parser. The defaults are 1,000 parts, 16,384 bytes of each part's header section (padding after its delimiter included) and 100 fields per part. Tagged `#Final`, `#Immutable`, `#ThreadSafe`.
- **Multipart.MultipartLimits** · [`export last max_parts: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L41)
  The most parts.
- **Multipart.MultipartLimits** · [`export last max_header_bytes: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L43)
  The most bytes of one part's header section, its empty line included.
- **Multipart.MultipartLimits** · [`export last max_header_fields: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L45)
  The most header fields of one part.
- **Multipart.MultipartLimits** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L48)
  The default limits.
- **Multipart.MultipartLimits** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L70)
  Equal when every limit is equal.
- **Multipart.MultipartLimits** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L80)
  Combines every limit.
- **Multipart.MultipartLimits** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L87)
  The limits as text.
- **Multipart** · [`export let Part = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L93)
  One complete part: its header fields and body bytes. Tagged `#Final`, `#NotThreadSafe`.
- **Multipart.Part** · [`export last headers: http_headers.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L95)
  The part's header fields.
- **Multipart.Part** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L97)
  The part's body; the list is shared, not copied.
- **Multipart.Part** · [`export let init = func(fields: http_headers.Headers, content: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L100)
  A part of the given fields and body.
- **Multipart.Part** · [`export let disposition = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L108)
  The disposition type of Content-Disposition in lowercase ("form-data" in a form, RFC 7578 section 4.2), or None when the field is absent. Tagged `#NoDiscard`.
- **Multipart.Part** · [`export let name = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L123)
  The "name" parameter of Content-Disposition, quoted-pairs undone (RFC 7578 section 4.2); None when the field or parameter is absent or the field is malformed. Tagged `#NoDiscard`.
- **Multipart.Part** · [`export let filename = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L128)
  The "filename" parameter of Content-Disposition, as `name` reads it. It is not percent-decoded and not checked as a path (RFC 7578 section 4.2 leaves both to the application). Tagged `#NoDiscard`.
- **Multipart.Part** · [`export let content_type = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L132)
  The Content-Type field, or "text/plain" when absent (RFC 7578 section 4.4, RFC 2046 section 5.1.1).
- **Multipart.Part** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L138)
  Equal header maps and equal body bytes.
- **Multipart.Part** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L155)
  Combines the header map and the body bytes.
- **Multipart.Part** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L164)
  The header map and the body length.
- **Multipart** · [`export let MultipartParser = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L174)
  A streaming parser of one multipart body. Feed the body in pieces split anywhere; each `feed` returns the events its bytes completed. After the close delimiter, further bytes are the epilogue and are ignored. Call `finish` at the end of the body. After an error every call returns the same error. Tagged `#Final`, `#NotThreadSafe`.
- **Multipart.MultipartParser** · [`export let init = func(boundary: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L196)
  A parser for `boundary` with the default limits. Traps with "invalid argument: boundary" unless `is_boundary(boundary)`.
- **Multipart.MultipartParser** · [`export let feed = func(chunk: uint8[list]) -> Result<MultipartEvent[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L210)
  Parses the next bytes of the body; Ok holds the events they completed, possibly none. Tagged `#NoDiscard`.
- **Multipart.MultipartParser** · [`export let finish = func -> Result<MultipartEvent[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L318)
  Ends the body: Ok with no events after the close delimiter, else 400 (the close delimiter is missing). Tagged `#NoDiscard`.
- **Multipart.MultipartParser** · [`export let is_done = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L329)
  Whether the close delimiter has been read.
- **Multipart.MultipartParser** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L332)
  The parser's progress as text.
- **Multipart** · [`export let boundary_of = func(content_type: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L412)
  The boundary parameter of a multipart Content-Type value such as `multipart/form-data; boundary=AaB03x`, quoted or not (RFC 2046 section 5.1.1, RFC 7578 section 4.1). 415 when the media type is not multipart/<subtype>; 400 when the parameters are malformed (RFC 9110 section 5.6.6: an unquoted value must be a token), the boundary is missing or given twice, or it is not 1 to 70 bchars without a final space (`is_boundary`). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Multipart** · [`export let is_boundary = func(text: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L446)
  Whether `text` is a boundary: 1 to 70 bchars (digits, letters, space and '()+_,-./:=?) not ending in a space (RFC 2046 section 5.1.1). Tagged `#Pure`, `#ThreadSafe`.
- **Multipart** · [`export let parameter = func(value: string, name: string) -> Result<Optional<string>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L466)
  The value of parameter `name` (compared without case) of a field value of the form `type *( ";" name "=" value )` (RFC 9110 section 5.6.6), where type is a token or token "/" token, quoted-pairs undone. Ok(None) when absent; 400 when the parameters are malformed. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Multipart** · [`export let parse_all = func(body: uint8[list], boundary: string, limits: MultipartLimits) -> Result<Part[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/multipart.nra#L485)
  The parts of a complete multipart body with `boundary`. 400 for an invalid boundary or a missing close delimiter; other errors as `MultipartParser.feed`. A part's body is held in memory; use `MultipartParser` for large parts. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
