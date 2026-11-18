# `Std.Crypto.Symmetric.ChaCha20`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Symmetric.ChaCha20`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Symmetric.ChaCha20`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ChaCha20** · [`export let xor = func(key: uint8[list], nonce: uint8[list], counter: uint32, bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20.nra#L18)
  The bytes XORed with the ChaCha20 keystream for a 32-byte key and 12-byte nonce, starting at block `counter`. Encryption and decryption are the same call. Traps on another key or nonce length, and when the block counter would pass 2^32 - 1. Tagged `#NoDiscard`. Since 0.3.0.
- **ChaCha20** · [`export let xchacha20_xor = func(key: uint8[list], nonce: uint8[list], counter: uint32, bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20.nra#L27)
  XChaCha20 (draft-irtf-cfrg-xchacha-03): ChaCha20 under HChaCha20(key, nonce[0:16]) with the nonce 0^4 || nonce[16:24], for a 24-byte nonce. Tagged `#NoDiscard`. Since 0.3.0.
- **ChaCha20** · [`export let hchacha20 = func(key: uint8[list], nonce: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20.nra#L35)
  HChaCha20 of a 32-byte key and a 16-byte input: a 32-byte subkey. Tagged `#NoDiscard`. Since 0.3.0.
- **ChaCha20** · [`export let short_nonce = func(nonce: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/cha-cha20.nra#L43)
  0^4 || nonce[16:24]: the ChaCha20 nonce of an XChaCha20 nonce. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#207-stdcryptosymmetric). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
