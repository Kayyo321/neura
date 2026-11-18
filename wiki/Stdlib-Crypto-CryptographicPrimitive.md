# `Std.Crypto.CryptographicPrimitive`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.CryptographicPrimitive`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/cryptographic-primitive.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.CryptographicPrimitive`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CryptographicPrimitive** · [`export let CryptographicPrimitive = interface<Alg>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/cryptographic-primitive.nra#L10)
  An algorithm implementation. `Alg` names the algorithm at the type level: the implementing class itself for digests, the key type for ciphers. `algorithm` names it for Std.Crypto.Core.CryptoPolicy. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#211-contracts). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
