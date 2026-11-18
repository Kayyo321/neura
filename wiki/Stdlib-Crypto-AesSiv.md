# `Std.Crypto.Symmetric.AesSiv`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Symmetric.AesSiv`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Symmetric.AesSiv`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AesSiv** · [`export let AesSiv = class implements AeadCipher<SymmetricKey<AesSiv>, uint8[list], uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L26)
  AES-SIV with a 32-, 48- or 64-byte key: the first half keys CMAC, the second half CTR. As an `AeadCipher`, `seal(nonce, plaintext, data)` uses the items [data] for an empty nonce (deterministic) and [data, nonce] otherwise, so a nonce may have any length. `seal_items` takes RFC 5297's item vector. Tagged `#Final`. Since 0.3.0.
- **AesSiv.AesSiv** · [`export let init = func(key: SymmetricKey<AesSiv>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L32)
  Keys the cipher with a 32-, 48- or 64-byte key (typed, or raw bytes); another length traps.
- **AesSiv.AesSiv** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L52)
  `Aes256Siv`, `Aes384Siv` or `Aes512Siv` (named by the total key size).
- **AesSiv.AesSiv** · [`export let nonce_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L54)
  0: AES-SIV takes a nonce of any length, or none.
- **AesSiv.AesSiv** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L56)
  16, the synthetic IV.
- **AesSiv.AesSiv** · [`export let seal = func(nonce: uint8[list], plaintext: uint8[list], associated_data: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L58)
- **AesSiv.AesSiv** · [`export let open = func(nonce: uint8[list], sealed: uint8[list], associated_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L61)
- **AesSiv.AesSiv** · [`export let seal_items = func(items: uint8[list][list], plaintext: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L74)
  The IV followed by the ciphertext, for up to 126 associated-data items (more trap). Tagged `#NoDiscard`.
- **AesSiv.AesSiv** · [`export let open_items = func(items: uint8[list][list], sealed: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L83)
  The plaintext of `seal_items`' output, or the "crypto.authentication" error. Tagged `#NoDiscard`.
- **AesSiv.AesSiv** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/aes-siv.nra#L144)
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#207-stdcryptosymmetric). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
