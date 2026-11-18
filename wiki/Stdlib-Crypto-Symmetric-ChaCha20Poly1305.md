# `Std.Crypto.Symmetric.ChaCha20Poly1305`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Symmetric.ChaCha20Poly1305`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Symmetric.ChaCha20Poly1305`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ChaCha20Poly1305** · [`export let ChaCha20Poly1305 = class implements AeadCipher<SymmetricKey<ChaCha20Poly1305>, uint8[list], uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L22)
  The AEAD under a 32-byte key. Tagged `#Final`. Since 0.3.0.
- **ChaCha20Poly1305.ChaCha20Poly1305** · [`export let init = func(key: SymmetricKey<ChaCha20Poly1305>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L26)
  Keys the cipher with a 32-byte key (typed, or raw bytes); another length traps.
- **ChaCha20Poly1305.ChaCha20Poly1305** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L39)
  `ChaCha20Poly1305`.
- **ChaCha20Poly1305.ChaCha20Poly1305** · [`export let nonce_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L41)
  12.
- **ChaCha20Poly1305.ChaCha20Poly1305** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L43)
  16.
- **ChaCha20Poly1305.ChaCha20Poly1305** · [`export let seal = func(nonce: uint8[list], plaintext: uint8[list], associated_data: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L45)
- **ChaCha20Poly1305.ChaCha20Poly1305** · [`export let open = func(nonce: uint8[list], sealed: uint8[list], associated_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L50)
- **ChaCha20Poly1305.ChaCha20Poly1305** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L56)
- **ChaCha20Poly1305** · [`export let seal_with = func(key: uint8[list], nonce: uint8[list], plaintext: uint8[list], data: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L78)
  Seals under a 32-byte key and 12-byte nonce. Used by XChaCha20Poly1305 with its subkey. Since 0.3.0.
- **ChaCha20Poly1305** · [`export let open_with = func(key: uint8[list], nonce: uint8[list], sealed: uint8[list], data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20-poly1305.nra#L86)
  Opens under a 32-byte key and 12-byte nonce. Used by XChaCha20Poly1305 with its subkey. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#217-stdcryptosymmetric). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
