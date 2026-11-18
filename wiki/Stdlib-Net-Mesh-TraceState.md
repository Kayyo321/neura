# `Std.Net.Mesh.TraceState`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.TraceState`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.TraceState`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TraceState** · [`export const MaxMembers: int = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L22)
  The most list-members a `tracestate` may hold.
- **TraceState** · [`export const MaxPartLength: int = 256`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L24)
  The longest key and the longest value, in characters.
- **TraceState** · [`export const LongMember: int = 128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L26)
  The length above which a member is removed first when truncating.
- **TraceState** · [`export const MinPropagated: int = 512`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L28)
  The combined length every vendor should propagate at least.
- **TraceState** · [`export let TraceState = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L32)
  An ordered list of vendor entries, leftmost first. Tagged `#Final`.
- **TraceState.TraceState** · [`export let init = func(key_list: string[list], value_list: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L39)
  A state over parallel key and value lists (copied). Use `parse`, `empty` and `put`.
- **TraceState.TraceState** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L53)
  The number of list-members. Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L57)
  Whether there are no list-members. Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let key_at = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L61)
  The key at `index` (0 is the leftmost). Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let value_at = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L65)
  The value at `index`. Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let get = func(key: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L69)
  The value of `key`. Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let put = func(key: string, value: string) -> Result<TraceState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L77)
  A state with `key` set to `value` and moved to the front; the right-most member is dropped when the list would exceed 32. Fails with a Parse error when the key or value is invalid. Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let remove = func(key: string) -> TraceState`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L99)
  A state without `key`; the others keep their order. Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let truncated = func(max_length: int) -> TraceState`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L114)
  A state whose header is at most `max_length` characters: whole members are removed, first those longer than 128 characters (right to left), then from the right (spec "tracestate Limits"). Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let to_header = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L138)
  The header value: `key=value` members joined by commas without spaces ("" when empty). Tagged `#NoDiscard`.
- **TraceState.TraceState** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L147)
  Equal when the members and their order are.
- **TraceState.TraceState** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L155)
  The hash of the header value.
- **TraceState.TraceState** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L158)
  The header value.
- **TraceState** · [`export let empty = func -> TraceState`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L173)
  A state with no members. Tagged `#NoDiscard`.
- **TraceState** · [`export let parse = func(text: string) -> Result<TraceState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L177)
  Parses a combined `tracestate` value by the rules described for the module. Tagged `#NoDiscard`.
- **TraceState** · [`export let is_valid_key = func(key: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L218)
  Whether `key` is a valid list-member key. Tagged `#NoDiscard`.
- **TraceState** · [`export let is_valid_value = func(value: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-state.nra#L238)
  Whether `value` is a valid list-member value: 1 to 256 characters of 0x20 to 0x7E other than `,` and `=`, the last not a space. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
