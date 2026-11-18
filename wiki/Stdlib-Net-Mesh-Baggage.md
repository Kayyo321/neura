# `Std.Net.Mesh.Baggage`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.Baggage`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.Baggage`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Baggage** · [`export const MaxMembers: int = 64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L26)
  The most list-members `to_header` emits.
- **Baggage** · [`export const MaxBytes: int = 8192`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L28)
  The most bytes `to_header` emits.
- **Baggage** · [`export const MaxParsedMembers: int = 180`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L30)
  The most list-members `parse` accepts (`0*179` repetitions after the first).
- **Baggage** · [`export let Property = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L34)
  One property of a list-member: a key with an optional decoded value. Tagged `#Final`, `#Immutable`.
- **Baggage.Property** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L36)
  The property key, a token.
- **Baggage.Property** · [`export last value: Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L38)
  The decoded value, absent for a key-only property.
- **Baggage.Property** · [`export let init = func(name: string, decoded: Optional<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L41)
  A property. A key that is not a token traps with "invalid argument: key".
- **Baggage.Property** · [`export let to_header = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L51)
  The encoded form, `key` or `key=value`. Tagged `#NoDiscard`.
- **Baggage.Property** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L57)
  Equal when the key and value are.
- **Baggage.Property** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L61)
  The hash of the encoded form.
- **Baggage.Property** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L64)
  The encoded form.
- **Baggage** · [`export let Entry = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L69)
  One list-member: key, decoded value, and properties in order. Tagged `#Final`.
- **Baggage.Entry** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L71)
  The key, a token.
- **Baggage.Entry** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L73)
  The decoded value.
- **Baggage.Entry** · [`export let init = func(name: string, decoded: string, properties: Property[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L79)
  An entry (the property list is copied). A key that is not a token traps with "invalid argument: key".
- **Baggage.Entry** · [`export let property_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L93)
  The number of properties. Tagged `#NoDiscard`.
- **Baggage.Entry** · [`export let property_at = func(index: int) -> Property`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L97)
  The property at `index`. Tagged `#NoDiscard`.
- **Baggage.Entry** · [`export let to_header = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L101)
  The encoded member, `key=value;prop;prop=value`, without spaces. Tagged `#NoDiscard`.
- **Baggage.Entry** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L110)
  Equal when the encoded members are.
- **Baggage.Entry** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L114)
  The hash of the encoded member.
- **Baggage.Entry** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L117)
  The encoded member.
- **Baggage** · [`export let Baggage = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L122)
  The list-members of one baggage, in order; duplicate keys are kept as received. Tagged `#Final`.
- **Baggage.Baggage** · [`export let init = func(entries: Entry[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L127)
  A baggage over `entries` (copied).
- **Baggage.Baggage** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L136)
  The number of list-members. Tagged `#NoDiscard`.
- **Baggage.Baggage** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L140)
  Whether there are no list-members. Tagged `#NoDiscard`.
- **Baggage.Baggage** · [`export let entry_at = func(index: int) -> Entry`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L144)
  The member at `index`. Tagged `#NoDiscard`.
- **Baggage.Baggage** · [`export let get = func(key: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L148)
  The value of the first member named `key`. Tagged `#NoDiscard`.
- **Baggage.Baggage** · [`export let set = func(key: string, value: string) -> Baggage`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L161)
  A baggage where `key` has `value` and no properties: the first member named `key` is replaced in place and later ones removed, or the member is appended. A key that is not a token traps with "invalid argument: key". Tagged `#NoDiscard`.
- **Baggage.Baggage** · [`export let remove = func(key: string) -> Baggage`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L181)
  A baggage without the members named `key`. Tagged `#NoDiscard`.
- **Baggage.Baggage** · [`export let to_header = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L194)
  The header value within the propagation limits: whole members from the left while the result holds at most 64 members and 8192 bytes; members are joined by "," without spaces. Tagged `#NoDiscard`.
- **Baggage.Baggage** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L210)
  Equal when every member and the order are.
- **Baggage.Baggage** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L227)
  Combines the member hashes.
- **Baggage.Baggage** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L236)
  Text such as "Baggage(3 members)".
- **Baggage** · [`export let empty = func -> Baggage`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L241)
  A baggage with no members. Tagged `#NoDiscard`.
- **Baggage** · [`export let parse = func(text: string) -> Result<Baggage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L245)
  Parses a combined `baggage` value by the rules described for the module. Tagged `#NoDiscard`.
- **Baggage** · [`export let is_baggage_octet = func(b: uint8) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L308)
  Whether `b` is a baggage-octet: 0x21, 0x23-0x2B, 0x2D-0x3A, 0x3C-0x5B, 0x5D-0x7E. Tagged `#NoDiscard`.
- **Baggage** · [`export let encode = func(value: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L313)
  `value` with every byte of its UTF-8 form outside `baggage-octet`, and `%`, percent-encoded. Tagged `#NoDiscard`.
- **Baggage** · [`export let decode = func(text: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/baggage.nra#L330)
  The decoded value, or None when a byte is not a baggage-octet or a `%` is not a full triplet. Decoded bytes that are not UTF-8 become U+FFFD. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
