# `Std.Crypto.Mac.Poly1305`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Mac.Poly1305`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Mac.Poly1305`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Poly1305** · [`export let Poly1305 = class implements Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L17)
  Poly1305 under a 32-byte one-time key (r, then s). A key must never authenticate two messages; the AEADs of Std.Crypto.Symmetric derive a fresh one per nonce. Tagged `#Final`. Since 0.3.0.
- **Poly1305.Poly1305** · [`export let init = func(key: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L33)
  Takes the 32-byte one-time key; any other length traps.
- **Poly1305.Poly1305** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L49)
  `Algorithm.Poly1305`.
- **Poly1305.Poly1305** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L51)
  16.
- **Poly1305.Poly1305** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L53)
- **Poly1305.Poly1305** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L109)
- **Poly1305.Poly1305** · [`export let verify = func(tag: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L169)
- **Poly1305.Poly1305** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L171)
- **Poly1305** · [`export let mac = func(key: uint8[list], message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/poly1305.nra#L176)
  The 16-byte Poly1305 tag of the message under the one-time key. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#215-stdcryptomac). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
