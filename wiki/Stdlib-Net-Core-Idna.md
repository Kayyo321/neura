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

- **Idna** · [`export const MaxLabelLength: int = 63`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L24)
  The longest label in bytes of its ASCII form (RFC 1035 section 2.3.4).
- **Idna** · [`export const MaxNameLength: int = 253`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L26)
  The longest name in bytes of its ASCII form, without a final root dot.
- **Idna** · [`export let to_ascii = func(domain: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L35)
  The ASCII (A-label) form of a domain name whose labels are already UTS 46-mapped. Errors (Parse, offset is the byte offset in the input of the failing label): Empty for an empty name or an empty label other than a final root label; InvalidCharacter for a scalar that needs UTS 46 mapping or is disallowed, or a hyphen rule violation; Syntax for an `xn--` label that is not valid Punycode of a non-ASCII label; OutOfRange for a label over 63 bytes or a name over 253 bytes. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Idna** · [`export let to_ascii_strict = func(domain: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L40)
  to_ascii with the STD3 ASCII rules (UTS 46 UseSTD3ASCIIRules): an ASCII scalar in a label must also be a letter, a digit, or `-`, as in a host name (RFC 1123 section 2.1). Errors as in to_ascii. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Idna** · [`export let to_unicode = func(domain: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L48)
  The Unicode (U-label) form of a domain name: ASCII letters lowercased, each `xn--` label Punycode-decoded, dot variants replaced by `.`, a final root dot kept. Errors (Parse, offset of the failing label): Empty as in to_ascii; Syntax for an `xn--` label that does not decode. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Idna** · [`export let map_uts46 = func(domain: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L81)
  Maps a name with the Unicode 17.0.0 UTS 46 nontransitional profile and normalizes it to NFC. Retains deviation and disallowed scalars for subsequent label validation. This call does not decode A-labels or validate the mapped name. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Idna** · [`export let is_dot = func(scalar: uint32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/idna.nra#L85)
  Tests whether a scalar is `.` or one of the IDNA dot variants U+3002, U+FF0E, U+FF61. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

Use `map_uts46` before A-label conversion for names typed as Unicode. Mapping applies Unicode 17.0.0 nontransitional UTS 46 and NFC without a host service. The A-label converter applies the hyphen, length and optional STD3 rules. See [the standard library specification](Stdlib-Design#114-stdnet) for the scope of validation.
