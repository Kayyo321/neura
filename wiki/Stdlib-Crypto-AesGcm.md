# `Std.Crypto.Symmetric.AesGcm`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Symmetric.AesGcm`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Symmetric.AesGcm`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AesGcm** · [`export let AesGcm = class implements AeadCipher<SymmetricKey<AesGcm>, uint8[list], uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra#L22)
  AES-128-GCM, AES-192-GCM or AES-256-GCM, chosen by the key length. Tagged `#Final`. Since 0.3.0.
- **AesGcm.AesGcm** · [`export let init = func(key: SymmetricKey<AesGcm>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra#L28)
  Keys the cipher with a 16-, 24- or 32-byte key (typed, or raw bytes); another length traps.
- **AesGcm.AesGcm** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra#L47)
  `Aes128Gcm`, `Aes192Gcm` or `Aes256Gcm`.
- **AesGcm.AesGcm** · [`export let nonce_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra#L49)
  12.
- **AesGcm.AesGcm** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra#L51)
  16.
- **AesGcm.AesGcm** · [`export let seal = func(nonce: uint8[list], plaintext: uint8[list], associated_data: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra#L53)
- **AesGcm.AesGcm** · [`export let open = func(nonce: uint8[list], sealed: uint8[list], associated_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra#L61)
- **AesGcm.AesGcm** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-gcm.nra#L108)
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#207-stdcryptosymmetric). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
