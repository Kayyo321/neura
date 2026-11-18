# `Std.Crypto.Symmetric.AesBlock`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Symmetric.AesBlock`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-block.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Symmetric.AesBlock`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AesBlock** · [`export let AesBlock = class implements CryptographicPrimitive<AesBlock>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-block.nra#L17)
  An expanded AES-128, AES-192 or AES-256 key. Tagged `#Final`. Since 0.3.0.
- **AesBlock.AesBlock** · [`export let init = func(key: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-block.nra#L23)
  Expands a 16-, 24- or 32-byte key; another length traps. Checks the policy for Aes128, Aes192 or Aes256.
- **AesBlock.AesBlock** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-block.nra#L34)
  `Aes128`, `Aes192` or `Aes256`.
- **AesBlock.AesBlock** · [`export let encrypt_block = func(block: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-block.nra#L37)
  The encryption of one 16-byte block; another length traps.
- **AesBlock.AesBlock** · [`export let decrypt_block = func(block: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-block.nra#L43)
  The decryption of one 16-byte block; another length traps.
- **AesBlock.AesBlock** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-block.nra#L54)
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#217-stdcryptosymmetric). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
