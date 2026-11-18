# `Std.Crypto.Kdf.Pbkdf2`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Kdf.Pbkdf2`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/pbkdf2.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Kdf.Pbkdf2`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Pbkdf2** · [`export let derive = func<H: StreamingDigest<H>>(password: uint8[list], salt: uint8[list], iterations: int, length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/pbkdf2.nra#L15)
  `length` bytes derived from the password and salt with `iterations` rounds of HMAC-H. Fewer than one iteration or a negative length traps. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#216-stdcryptokdf). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
