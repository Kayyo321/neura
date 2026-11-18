# `Std.Net.Rpc.Metadata`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Metadata`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Metadata`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Metadata** · [`export const BinarySuffix: string = "-bin"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L22)
  The suffix of a binary header name.
- **Metadata** · [`export const FieldOverhead: int = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L24)
  The overhead per field in the header list size (as HTTP/2 `SETTINGS_MAX_HEADER_LIST_SIZE`).
- **Metadata** · [`export let is_valid_name = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L28)
  Whether `name` is a valid header name: 1 or more of `0-9 a-z _ - .`. Tagged `#NoDiscard`.
- **Metadata** · [`export let is_valid_ascii_value = func(value: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L41)
  Whether `value` is a valid ASCII value: 1 or more bytes in 0x20 to 0x7E. Tagged `#NoDiscard`.
- **Metadata** · [`export let is_binary_name = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L56)
  Whether `name` names a binary header. Tagged `#NoDiscard`.
- **Metadata** · [`export let decode_binary = func(value: string) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L61)
  Decodes a binary header value: splits on `,`, then decodes each part as padded or unpadded base64 and joins the bytes of the parts in order. Tagged `#NoDiscard`.
- **Metadata** · [`export let Metadata = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L76)
  Ordered gRPC metadata entries as they travel: names lower-case, binary values base64. Tagged `#Final`, `#NotThreadSafe`.
- **Metadata.Metadata** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L83)
  Empty metadata.
- **Metadata.Metadata** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L88)
  The number of entries. Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let add = func(name: string, value: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L93)
  Adds an ASCII entry. The name is lower-cased first. Fails with a Parse error for an invalid name, a `-bin` name (use `add_binary`), or an invalid value. Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let add_binary = func(name: string, value: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L107)
  Adds a binary entry, stored as unpadded base64. The name must end in `-bin`. Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let get = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L119)
  The first value of `name` as sent (base64 for a binary name). Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let get_all = func(name: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L131)
  Every value of `name` in order. Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let get_binary = func(name: string) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L144)
  Every decoded value of the binary header `name`, after splitting joined values on `,`. Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let contains = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L156)
  Whether an entry named `name` exists. Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let remove = func(name: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L159)
  Removes every entry named `name`; returns how many were removed.
- **Metadata.Metadata** · [`export let add_raw = func(name: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L177)
  Appends an entry as received, without checks: `name` must already be lower-case and a binary value must be base64 text. `from_entries` uses it.
- **Metadata.Metadata** · [`export let add_all = func(other: Metadata)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L183)
  Appends every entry of `other`.
- **Metadata.Metadata** · [`export let entries = func -> [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L193)
  The entries as `[name, value]` pairs in order (a copy). Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let header_list_size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L203)
  The header list size: for each entry its name and value lengths plus 32. Tagged `#NoDiscard`.
- **Metadata.Metadata** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L212)
  Equal when the entries are equal in order.
- **Metadata.Metadata** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L229)
  Combines every name and value.
- **Metadata.Metadata** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L238)
  `name: value` lines joined by `, `.
- **Metadata** · [`export let empty = func -> Metadata`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L249)
  New empty metadata. Tagged `#NoDiscard`.
- **Metadata** · [`export let from_entries = func(pairs: [string, string][list]) -> Metadata`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/metadata.nra#L255)
  Metadata from received header pairs. Names are lower-cased; pseudo-headers (`:`), invalid names and ASCII entries with invalid values are dropped; binary values are kept as received and decoded by `get_binary`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
