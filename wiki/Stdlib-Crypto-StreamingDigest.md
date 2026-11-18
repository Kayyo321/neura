# `Std.Crypto.StreamingDigest`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.StreamingDigest`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/streaming-digest.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.StreamingDigest`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **StreamingDigest** · [`export let StreamingDigest = interface<D>:CryptographicPrimitive<D>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/streaming-digest.nra#L12)
  A running digest whose type argument `D` is the implementing class itself, as in `Sha256 implements StreamingDigest<Sha256>`. Neura has no value type parameters, so the specification's `OutLen` is the static `output_length`. Generic code such as `Hmac<H: StreamingDigest<H>>` makes a fresh digest with `H.create()`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#201-contracts). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
