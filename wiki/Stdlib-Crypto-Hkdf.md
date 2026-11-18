# `Std.Crypto.Kdf.Hkdf`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Kdf.Hkdf`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/hkdf.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Kdf.Hkdf`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Hkdf** · [`export let extract = func<H: StreamingDigest<H>>(salt: uint8[list], input_key: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/hkdf.nra#L17)
  The pseudorandom key HMAC-H(salt, input key material). An empty salt stands for H's output length of zero bytes, as the RFC says. Tagged `#NoDiscard`. Since 0.3.0.
- **Hkdf** · [`export let expand = func<H: StreamingDigest<H>>(prk: uint8[list], info: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/hkdf.nra#L26)
  `length` bytes of output keying material from the pseudorandom key and the info label. A length above 255 times H's output length traps with "invalid argument: HKDF output too long". Tagged `#NoDiscard`. Since 0.3.0.
- **Hkdf** · [`export let derive = func<H: StreamingDigest<H>>(salt: uint8[list], input_key: uint8[list], info: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/hkdf.nra#L50)
  `expand(extract(salt, input_key), info, length)`. Tagged `#NoDiscard`. Since 0.3.0.
- **Hkdf** · [`export let expand_label = func<H: StreamingDigest<H>>(prk: uint8[list], label: string, length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/hkdf.nra#L60)
  `expand` with the UTF-8 bytes of a text label as the info. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#206-stdcryptokdf). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
