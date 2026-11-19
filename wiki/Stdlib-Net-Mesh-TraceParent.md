# `Std.Net.Mesh.TraceParent`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.TraceParent`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.TraceParent`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TraceParent** · [`export const SampledFlag: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L20)
  The sampled flag (bit 0): the caller may have recorded trace data.
- **TraceParent** · [`export const RandomTraceIdFlag: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L22)
  The random-trace-id flag (bit 1): at least the right-most 7 bytes of the trace id are random.
- **TraceParent** · [`export const KnownFlags: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L24)
  The flags this version defines; a component sets every other flag to zero when it writes a header.
- **TraceParent** · [`export const Length: int = 55`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L26)
  The length of a version-00 header value.
- **TraceParent** · [`export const TraceIdBytes: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L28)
  The trace id length in bytes.
- **TraceParent** · [`export const ParentIdBytes: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L30)
  The parent id length in bytes.
- **TraceParent** · [`export let TraceParent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L34)
  One parsed or generated `traceparent` value. Tagged `#Final`.
- **TraceParent.TraceParent** · [`export last version: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L37)
  The version the value was parsed from (0 for a value this library made). Emission always writes version 00.
- **TraceParent.TraceParent** · [`export last trace_id: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L39)
  The 16-byte trace id; never all zero.
- **TraceParent.TraceParent** · [`export last parent_id: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L41)
  The 8-byte id of the caller's span; never all zero.
- **TraceParent.TraceParent** · [`export last flags: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L43)
  The 8-bit trace flags as received or set.
- **TraceParent.TraceParent** · [`export let init = func(parsed_version: int, trace: uint8[list], parent: uint8[list], trace_flags: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L47)
  A value with every field given. Wrong lengths, all-zero ids, flags outside 0 to 255 or a version outside 0 to 254 trap with "invalid argument: <name>"; use `parse` for untrusted text.
- **TraceParent.TraceParent** · [`export let sampled = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L68)
  Whether the sampled flag is set. Tagged `#NoDiscard`.
- **TraceParent.TraceParent** · [`export let random_trace_id = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L72)
  Whether the random-trace-id flag is set. Tagged `#NoDiscard`.
- **TraceParent.TraceParent** · [`export let trace_id_hex = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L76)
  The trace id as 32 lowercase hex digits. Tagged `#NoDiscard`.
- **TraceParent.TraceParent** · [`export let parent_id_hex = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L80)
  The parent id as 16 lowercase hex digits. Tagged `#NoDiscard`.
- **TraceParent.TraceParent** · [`export let to_header = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L84)
  The version-00 header value, with the flags this version does not define cleared. Tagged `#NoDiscard`.
- **TraceParent.TraceParent** · [`export let child = func(span_id: uint8[list]) -> TraceParent`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L91)
  The same trace continued by a new span: the same trace id, `span_id` as the parent id, the known flags kept (the random-trace-id flag must not change while the trace id is kept). Traps like `init` on a bad `span_id`. Tagged `#NoDiscard`.
- **TraceParent.TraceParent** · [`export let child_sampled = func(span_id: uint8[list], sample: bool) -> TraceParent`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L97)
  As `child` with the sampled flag set to `sample`; the spec allows changing it only together with the parent id. Tagged `#NoDiscard`.
- **TraceParent.TraceParent** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L101)
  Equal when the version, both ids and the flags are.
- **TraceParent.TraceParent** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L110)
  Combines the version, flags and id bytes.
- **TraceParent.TraceParent** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L122)
  The version-00 header value.
- **TraceParent** · [`export let parse = func(text: string) -> Result<TraceParent>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L127)
  Parses a `traceparent` value by the rules described for the module. Tagged `#NoDiscard`.
- **TraceParent** · [`export let random_id = func(random: Random, count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L167)
  `count` random bytes from `random` that are not all zero (drawn again in the rare all-zero case). Tagged `#NoDiscard`.
- **TraceParent** · [`export let start = func(random: Random, sample: bool) -> TraceParent`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-parent.nra#L178)
  A new trace: a random trace id (with the random-trace-id flag set), a random parent id, and the sampled flag set to `sample`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
