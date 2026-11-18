# `Std.Net.Core.Punycode`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.Punycode`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/punycode.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.Punycode`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Punycode** · [`export let encode = func(code_points: uint32[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/punycode.nra#L33)
  The Punycode encoding of the code points, without any ACE prefix. Errors: Parse OutOfRange at the index of a value that is not a Unicode scalar value (a surrogate 0xD800-0xDFFF or above 0x10FFFF), or at 0 when the delta arithmetic would pass maxint. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Punycode** · [`export let decode = func(text: string) -> Result<uint32[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/punycode.nra#L111)
  The code points a Punycode string encodes (the text after any ACE prefix). Errors (Parse, offset in bytes of the input): InvalidCharacter for a non-ASCII byte before the last delimiter or a byte that is not a base-36 digit after it; UnexpectedEnd when the last variable-length integer is cut short; OutOfRange when the arithmetic passes maxint or a decoded value is basic (below 0x80), a surrogate, or above 0x10FFFF. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Punycode** · [`export let encode_text = func(text: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/punycode.nra#L181)
  The Punycode encoding of the text's scalar values. Errors as in encode (only overflow can occur). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Punycode** · [`export let decode_text = func(text: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/punycode.nra#L185)
  The text a Punycode string encodes. Errors as in decode. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
