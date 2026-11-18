# `Std.Net.Dns.Name`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.Name`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.Name`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Name** · [`export const MaxLabelLength: int = 63`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L18)
  The longest label in bytes (RFC 1035 section 2.3.4).
- **Name** · [`export const MaxNameLength: int = 255`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L20)
  The longest name in wire bytes, length bytes and root byte included (RFC 1035 section 2.3.4).
- **Name** · [`export let DnsName = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L24)
  An absolute domain name. Tagged `#Final`.
- **Name.DnsName** · [`export let init = func(labels: uint8[list][list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L31)
  A name of `labels` (leftmost first; empty for the root). An empty label, a label above 63 bytes or a name above 255 wire bytes traps with "invalid argument: dns name"; use `from_labels` to get an error instead.
- **Name.DnsName** · [`export let label_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L44)
  The number of labels; 0 for the root. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let label = func(index: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L48)
  The bytes of label `index` (a copy). An index out of range traps. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let label_text = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L52)
  Label `index` as escaped text. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let labels = func -> uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L56)
  The labels (copies). Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let is_root = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L66)
  Whether this is the root name. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let wire_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L70)
  The length of the uncompressed wire form. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let suffix = func(count: int) -> DnsName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L81)
  The name without its leftmost `count` labels; the root when `count` is the label count. A count out of range traps with "invalid argument: count". Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let concat = func(origin: DnsName) -> Result<DnsName>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L95)
  This name followed by `origin` (this name's labels, then the origin's). Err when the result is longer than 255 bytes. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let is_within = func(other: DnsName) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L109)
  Whether this name is `other` or lies below it, label by label and ignoring ASCII case: `a.corp.example.` is within `corp.example.`, `notcorp.example.` is not. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let to_lower = func -> DnsName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L125)
  The name with ASCII letters lowered. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let key = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L135)
  The lowered text form: a key for maps that ignores case. Tagged `#NoDiscard`.
- **Name.DnsName** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L138)
  Equal when the labels match ignoring ASCII case.
- **Name.DnsName** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L155)
  The hash of the lowered labels.
- **Name.DnsName** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L167)
  The master-file text with a trailing dot ("www.example.com."); "." for the root.
- **Name** · [`export let root = func -> DnsName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L181)
  The root name. Tagged `#NoDiscard`.
- **Name** · [`export let from_labels = func(labels: uint8[list][list]) -> Result<DnsName>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L186)
  A name of `labels`; Err (Parse, OutOfRange) for an empty label, a label above 63 bytes or a name above 255 wire bytes. Tagged `#NoDiscard`.
- **Name** · [`export let parse = func(text: string) -> Result<DnsName>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L199)
  Parses master-file text: labels separated by dots, an optional trailing dot, `\.`, `\\` and `\DDD` escapes. "" and "." are the root. Err (Parse) for an empty label, a bad escape, a label above 63 bytes or a name above 255 wire bytes. Tagged `#NoDiscard`.
- **Name** · [`export let same_label = func(a: uint8[list], b: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/name.nra#L260)
  Whether two labels are equal ignoring ASCII case. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
