# `Std.Net.Core.PercentEncoding`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.PercentEncoding`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.PercentEncoding`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PercentEncoding** · [`export let EncodeSet = enum ( Unreserved, UserInfo, RegName, PathSegment, Path, Query, QueryComponent, Fragment, Form )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L22)
  The bytes a component keeps unencoded, from the RFC 3986 appendix A grammar. Unreserved: ALPHA, DIGIT, `-`, `.`, `_`, `~`. UserInfo: unreserved, sub-delims, `:`. RegName: unreserved and sub-delims. PathSegment: pchar (unreserved, sub-delims, `:`, `@`). Path: pchar and `/`. Query and Fragment: pchar, `/`, `?`. QueryComponent: the Query set without `&`, `=`, `+`, for one key or value of a query string. Form: ALPHA, DIGIT, `*`, `-`, `.`, `_`, with space written as `+` (the WHATWG URL application/x-www-form-urlencoded serializer). `%` is never kept. Since 0.3.0.
- **PercentEncoding** · [`export let is_unreserved = func(b: uint8) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L31)
  Tests whether a byte is ALPHA, DIGIT, `-`, `.`, `_`, or `~` (RFC 3986 section 2.3). Tagged `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let is_sub_delim = func(b: uint8) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L38)
  Tests whether a byte is one of the sub-delims `!$&'()*+,;=` (RFC 3986 section 2.2). Tagged `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let is_allowed = func(b: uint8, set: EncodeSet) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L43)
  Tests whether the set keeps the byte unencoded. Tagged `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let encode = func(text: string, set: EncodeSet) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L60)
  The text with every byte the set does not keep percent-encoded in uppercase hex. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let encode_bytes = func(bytes: uint8[list], set: EncodeSet) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L71)
  The bytes as text, with every byte the set does not keep percent-encoded in uppercase hex. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let decode = func(text: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L91)
  The text with each `%XX` triplet decoded; `+` stays `+`. Errors: Parse InvalidCharacter at a `%` not followed by two hex digits; Encoding at the offset in the input of the first byte that starts an ill-formed UTF-8 sequence after decoding. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let decode_form = func(text: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L96)
  Decodes an application/x-www-form-urlencoded key or value: `+` becomes a space, then each `%XX` triplet is decoded. Errors as in decode. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let decode_bytes = func(text: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L102)
  The bytes of the text with each `%XX` triplet decoded, without any UTF-8 check. Errors: Parse InvalidCharacter at a `%` not followed by two hex digits. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let first_invalid = func(text: string, set: EncodeSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L113)
  The offset of the first byte that is neither kept by the set nor the start of a well-formed `%XX` triplet, or -1 when every byte is valid. Bytes of a triplet are skipped together. Tagged `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let normalize = func(text: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L134)
  Percent-encoding normalization (RFC 3986 sections 6.2.2.1 and 6.2.2.2): a triplet that encodes an unreserved byte is decoded, every other triplet gets uppercase hex digits. A `%` that does not start a well-formed triplet is copied unchanged. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let is_triplet = func(text: string, offset: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L162)
  Tests whether the text has a well-formed `%XX` triplet at the offset. Tagged `#Pure`, `#ThreadSafe`.
- **PercentEncoding** · [`export let hex_value = func(b: uint8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/percent-encoding.nra#L169)
  The value of an ASCII hex digit in either case, or -1. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
