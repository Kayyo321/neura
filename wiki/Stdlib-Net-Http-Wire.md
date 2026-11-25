# `Std.Net.Http.Wire`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Wire`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Wire`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Wire** · [`export let BodyFraming = enum (  None,  Length(int64),  Chunked,  UntilClose )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L30)
  How the length of a message body is found (RFC 9112 section 6.3).
- **Wire** · [`export let Limits = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L47)
  The limits a recipient applies to a message. Read-only once built. The defaults are those of the server host: a 16,384-byte head (Apps030 `S-5`), 100 header fields, a 1,048,576-byte body (`S-6`), a 4,096-byte chunk-size line with its extensions, a 16,384-byte trailer section, bare LF accepted in heads, and the nine standard methods as the known ones. Tagged `#Final`, `#Immutable`, `#ThreadSafe`.
- **Wire.Limits** · [`export last max_head_bytes: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L49)
  The most bytes of a head, empty line and ignored leading empty lines included.
- **Wire.Limits** · [`export last max_header_fields: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L51)
  The most field lines of a head or trailer section.
- **Wire.Limits** · [`export last max_body_bytes: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L53)
  The most bytes of a message body (Content-Length or decoded chunked data).
- **Wire.Limits** · [`export last max_chunk_line_bytes: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L55)
  The most bytes of one chunk-size line, extensions included, CRLF excluded.
- **Wire.Limits** · [`export last max_trailer_bytes: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L57)
  The most bytes of a chunked trailer section, its empty line included.
- **Wire.Limits** · [`export last allow_bare_lf: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L59)
  Whether a single LF ends a head line (RFC 9112 section 2.2).
- **Wire.Limits** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L64)
  The default limits.
- **Wire.Limits** · [`export let knows_method = func(method: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L113)
  Whether `method` (exact case) is one the server implements.
- **Wire.Limits** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L117)
  Equal when every limit and the known methods (in order) are equal.
- **Wire.Limits** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L128)
  Combines every limit and the known methods.
- **Wire.Limits** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L139)
  The limits as text.
- **Wire** · [`export let RequestHead = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L152)
  A request's start-line and header fields (RFC 9112 section 3). Tagged `#Final`, `#NotThreadSafe`.
- **Wire.RequestHead** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L154)
  The method token, exact case.
- **Wire.RequestHead** · [`export last target: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L156)
  The request-target as sent.
- **Wire.RequestHead** · [`export last version: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L158)
  The HTTP-version text, "HTTP/1.1" or "HTTP/1.0".
- **Wire.RequestHead** · [`export last headers: http_headers.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L160)
  The header fields; the map is shared, not copied.
- **Wire.RequestHead** · [`export let init = func(method_text: string, target_text: string, version_text: string, fields: http_headers.Headers)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L164)
  A head of the given parts. Traps with "invalid argument: <name>" when the method is not a token, the target is empty or holds a byte outside visible ASCII, or the version is not "HTTP/" DIGIT "." DIGIT.
- **Wire.RequestHead** · [`export let minor_version = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L182)
  The minor version digit.
- **Wire.RequestHead** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L185)
  Equal parts and equal header maps.
- **Wire.RequestHead** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L194)
  Combines the parts and the header map.
- **Wire.RequestHead** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L203)
  The request-line and the header map.
- **Wire** · [`export let ResponseHead = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L209)
  A response's status-line and header fields (RFC 9112 section 4). Tagged `#Final`, `#NotThreadSafe`.
- **Wire.ResponseHead** · [`export last version: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L211)
  The HTTP-version text.
- **Wire.ResponseHead** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L213)
  The status code, 100 through 599.
- **Wire.ResponseHead** · [`export last reason: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L215)
  The reason phrase, possibly empty.
- **Wire.ResponseHead** · [`export last headers: http_headers.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L217)
  The header fields; the map is shared, not copied.
- **Wire.ResponseHead** · [`export let init = func(version_text: string, status_code: int, reason_text: string, fields: http_headers.Headers)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L222)
  A head of the given parts. Traps with "invalid argument: <name>" when the version is not "HTTP/" DIGIT "." DIGIT, the status is outside 100 through 599, or the reason holds a control byte other than tab.
- **Wire.ResponseHead** · [`export let minor_version = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L240)
  The minor version digit.
- **Wire.ResponseHead** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L243)
  Equal parts and equal header maps.
- **Wire.ResponseHead** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L252)
  Combines the parts and the header map.
- **Wire.ResponseHead** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L261)
  The status-line and the header map.
- **Wire** · [`export let HeadReader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L272)
  Collects bytes until a complete head is buffered, enforcing the head limit as they arrive. The head ends at the first empty line after a non-empty line; empty lines before the start-line are part of the head and ignored by the parsers. Bytes after the head are kept as the remainder. A head with no end within `max_head_bytes` fails as soon as the buffer passes that size (431), so an endless head fails at the limit plus one byte. After a failure every `feed` returns the same error. Tagged `#Final`, `#NotThreadSafe`.
- **Wire.HeadReader** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L289)
  A reader with the default limits.
- **Wire.HeadReader** · [`export let feed = func(chunk: uint8[list]) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L300)
  Adds received bytes. Ok(true) once a complete head is buffered (later bytes join the remainder); Ok(false) while more are needed; 431 when the head passes `max_head_bytes`. Tagged `#NoDiscard`.
- **Wire.HeadReader** · [`export let is_complete = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L341)
  Whether a complete head is buffered.
- **Wire.HeadReader** · [`export let head_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L346)
  The head's bytes, empty line included, as a new list. Traps with "invalid state: head is not complete" before `feed` returned Ok(true). Tagged `#NoDiscard`.
- **Wire.HeadReader** · [`export let request_head = func -> Result<RequestHead>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L353)
  The buffered head parsed as a request head. Traps as `head_bytes` does when incomplete. Tagged `#NoDiscard`.
- **Wire.HeadReader** · [`export let response_head = func -> Result<ResponseHead>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L357)
  The buffered head parsed as a response head. Traps as `head_bytes` does when incomplete. Tagged `#NoDiscard`.
- **Wire.HeadReader** · [`export let remainder = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L361)
  The bytes received after the head, as a new list. Traps as `head_bytes` does when incomplete. Tagged `#NoDiscard`.
- **Wire.HeadReader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L367)
  The reader's progress as text.
- **Wire** · [`export let rejection = func(status: int, message: string) -> errors.Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L380)
  The error a server answers with `status`: a custom error whose code is "http.status.NNN". Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Wire** · [`export let rejection_status = func(e: errors.Error) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L385)
  The status of an error made by `rejection`, or 0 for any other error. Tagged `#Pure`, `#ThreadSafe`.
- **Wire** · [`export let parse_request_head = func(bytes: uint8[list], limits: Limits) -> Result<RequestHead>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L407)
  Parses the bytes of a request head, from the start-line (after any empty lines) through the empty line, against `limits`. Rejections: a head longer than `max_head_bytes` (431); a head that is not UTF-8, has a bare CR, a bare LF when not allowed, bytes after the empty line, or no empty line (400, RFC 9112 section 2.2); a request-line that is not method SP request-target SP HTTP-version, or a target with a byte outside visible ASCII (400, RFC 9112 section 3); a method that is not a token (400) or not known by `limits` (501); a version that is well formed but not HTTP/1.x (505); a field line with leading whitespace, i.e. obs-fold or whitespace after the start-line (400, RFC 9112 sections 5.2 and 2.2); whitespace between field name and colon (400, RFC 9112 section 5.1); a field name that is not a token or a value with a control byte (400); more than `max_header_fields` fields (431); a missing Host (in HTTP/1.0 too, as the Backend030 host rule says), more than one Host, or a Host value that is not a host and port (400, RFC 9112 section 3.2); Transfer-Encoding in HTTP/1.0 (400, RFC 9112 section 6.1); both Transfer-Encoding and Content-Length (400, RFC 9112 section 6.3 item 3); a transfer coding other than chunked (501, RFC 9112 section 6.1); chunked twice, with parameters, or an empty Transfer-Encoding (400, RFC 9112 section 7.1); a Content-Length that is not digits, or a list of differing values (400, RFC 9112 section 6.3 item 5); a Content-Length over `max_body_bytes` (413). Tagged `#NoDiscard`.
- **Wire** · [`export let parse_response_head = func(bytes: uint8[list], limits: Limits) -> Result<ResponseHead>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L476)
  Parses the bytes of a response head through the empty line, against `limits`. The checks are those of `parse_request_head` that apply to a status-line: head size (431), UTF-8 and line ends (400), a status-line that is not HTTP-version SP 3DIGIT [SP reason-phrase], a code outside 100 through 599 or a reason with a control byte (400, RFC 9112 section 4), a version that is not HTTP/1.x (505), whitespace after the status-line (400, RFC 9112 section 2.2), field syntax (400), the field count (431), Transfer-Encoding with HTTP/1.0 or with Content-Length (400) and an invalid Content-Length (400). Unlike a request, an obs-fold is replaced by one space (RFC 9112 section 5.2) and whitespace between a field name and its colon is removed (RFC 9112 section 5.1); a missing space after the code is accepted. Host, method and body-size rules do not apply. Tagged `#NoDiscard`.
- **Wire** · [`export let parse_field_section = func(text: string, limits: Limits, lenient: bool) -> Result<http_headers.Headers>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L532)
  Parses a field section: field lines, each ending in CRLF (or a single LF when `limits` allows), without the empty line that ends the section. Used for trailer sections and multipart part heads. Errors as for the fields of a head: 400 for a bad line end, an empty line, a line without a colon, a name that is not a token, whitespace before the colon (removed instead when `lenient`), a value with a control byte, or a line with leading whitespace (unfolded into the previous value instead when `lenient`, except on the first line); 431 past `max_header_fields`. Tagged `#NoDiscard`.
- **Wire** · [`export let request_body_length = func(head: RequestHead) -> Result<BodyFraming>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L541)
  The framing of a request's body (RFC 9112 section 6.3 items 3 to 7): Chunked when Transfer-Encoding is present (it must be exactly "chunked": another coding is 501, chunked twice or with parameters is 400), else Length for a valid Content-Length (400 otherwise), else None. Tagged `#NoDiscard`.
- **Wire** · [`export let response_body_length = func(head: ResponseHead, request_method: string) -> Result<BodyFraming>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L560)
  The framing of a response's body (RFC 9112 section 6.3) given the method of its request: None for a response to HEAD, a 1xx, 204 or 304 (item 1) and a 2xx to CONNECT, after which the connection is a tunnel (item 2); Chunked when chunked is the final transfer coding and UntilClose for other codings (item 4, Transfer-Encoding overriding Content-Length, item 3); Length for a valid Content-Length, 400 for an invalid one (items 5, 6); UntilClose otherwise (item 8). Tagged `#NoDiscard`.
- **Wire** · [`export let content_length = func(fields: http_headers.Headers) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L581)
  The value of the Content-Length fields: 1*DIGIT, or a list of equal such values (RFC 9112 section 6.3 item 5). 400 when absent, empty, not digits, differing, or above the int64 range. Tagged `#NoDiscard`.
- **Wire** · [`export let write_request_head = func(head: RequestHead) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L603)
  The head as bytes: the request-line, each field as "Name: value", each line ending in CRLF, then CRLF. Tagged `#NoDiscard`.
- **Wire** · [`export let write_response_head = func(head: ResponseHead) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L618)
  The head as bytes: the status-line, with the reason phrase of `Status.reason_phrase` when the head's reason is empty, then the fields as `write_request_head` writes them. Tagged `#NoDiscard`.
- **Wire** · [`export let is_version = func(text: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/wire.nra#L643)
  Whether `text` is "HTTP/" DIGIT "." DIGIT. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
