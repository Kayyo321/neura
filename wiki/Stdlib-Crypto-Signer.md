# `Std.Crypto.Signer`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Signer`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/signer.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Signer`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Signer** · [`export let Signer = interface<PrivKey, Sig>:CryptographicPrimitive<Sig>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/signer.nra#L10)
  Signs with a private key of type `PrivKey`, producing a `Sig`. The primitive is named by `Sig`, so one class can implement the Signer and the Verifier of a scheme. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#201-contracts). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
