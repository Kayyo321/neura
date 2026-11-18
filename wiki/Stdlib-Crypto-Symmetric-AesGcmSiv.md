# `Std.Crypto.Symmetric.AesGcmSiv`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Symmetric.AesGcmSiv`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Symmetric.AesGcmSiv`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AesGcmSiv** · [`export let AesGcmSiv = class implements AeadCipher<SymmetricKey<AesGcmSiv>, uint8[list], uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra#L23)
  AEAD_AES_128_GCM_SIV or AEAD_AES_256_GCM_SIV, chosen by the key length (16 or 32 bytes). Tagged `#Final`. Since 0.3.0.
- **AesGcmSiv.AesGcmSiv** · [`export let init = func(key: SymmetricKey<AesGcmSiv>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra#L29)
  Keys the cipher with a 16- or 32-byte key (typed, or raw bytes); another length traps.
- **AesGcmSiv.AesGcmSiv** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra#L46)
  `Aes128GcmSiv` or `Aes256GcmSiv`.
- **AesGcmSiv.AesGcmSiv** · [`export let nonce_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra#L48)
  12.
- **AesGcmSiv.AesGcmSiv** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra#L50)
  16.
- **AesGcmSiv.AesGcmSiv** · [`export let seal = func(nonce: uint8[list], plaintext: uint8[list], associated_data: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra#L52)
- **AesGcmSiv.AesGcmSiv** · [`export let open = func(nonce: uint8[list], sealed: uint8[list], associated_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra#L62)
- **AesGcmSiv.AesGcmSiv** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm-siv.nra#L138)
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#217-stdcryptosymmetric). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
