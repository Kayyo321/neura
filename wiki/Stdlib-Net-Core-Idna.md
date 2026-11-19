# `Std.Net.Core.Idna`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.Idna`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.Idna`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Idna** · [`export const MaxLabelLength: int = 63`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L25)
  The longest label in bytes of its ASCII form (RFC 1035 section 2.3.4).
- **Idna** · [`export const MaxNameLength: int = 253`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L27)
  The longest name in bytes of its ASCII form, without a final root dot.
- **Idna** · [`export let to_ascii = func(domain: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L36)
  The ASCII (A-label) form of a domain name whose labels are already UTS 46-mapped. Errors (Parse, offset is the byte offset in the input of the failing label): Empty for an empty name or an empty label other than a final root label; InvalidCharacter for a scalar that needs UTS 46 mapping or is disallowed, or a hyphen rule violation; Syntax for an `xn--` label that is not valid Punycode of a non-ASCII label; OutOfRange for a label over 63 bytes or a name over 253 bytes. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Idna** · [`export let to_ascii_strict = func(domain: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L41)
  to_ascii with the STD3 ASCII rules (UTS 46 UseSTD3ASCIIRules): an ASCII scalar in a label must also be a letter, a digit, or `-`, as in a host name (RFC 1123 section 2.1). Errors as in to_ascii. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Idna** · [`export let to_unicode = func(domain: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L49)
  The Unicode (U-label) form of a domain name: ASCII letters lowercased, each `xn--` label Punycode-decoded, dot variants replaced by `.`, a final root dot kept. Errors (Parse, offset of the failing label): Empty as in to_ascii; Syntax for an `xn--` label that does not decode. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Idna** · [`export let map_uts46 = func(domain: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L81)
  The UTS 46 mapping step (IdnaMappingTable lookup, case folding, NFC) of a domain name. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Idna** · [`export let is_dot = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L87)
  Tests whether a scalar is `.` or one of the IDNA dot variants U+3002, U+FF0E, U+FF61. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
