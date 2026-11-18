# `Std.Crypto.Mac.AesCmac`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Mac.AesCmac`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Mac.AesCmac`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AesCmac** · [`export let double = func(block: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L15)
  The block doubled in GF(2^128): shifted left one bit, with 0x87 folded in when the top bit was set (by mask, not by branch). Also used by AES-SIV's S2V. Since 0.3.0.
- **AesCmac** · [`export let AesCmac = class implements Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L26)
  AES-CMAC under a 16-, 24- or 32-byte key, with a 16-byte tag. Tagged `#Final`. Since 0.3.0.
- **AesCmac.AesCmac** · [`export let init = func(key: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L35)
  Keys the MAC; a key of another length traps.
- **AesCmac.AesCmac** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L44)
  `Algorithm.AesCmac`.
- **AesCmac.AesCmac** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L46)
  16.
- **AesCmac.AesCmac** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L48)
- **AesCmac.AesCmac** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L62)
- **AesCmac.AesCmac** · [`export let verify = func(tag: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L81)
- **AesCmac.AesCmac** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L83)
- **AesCmac** · [`export let mac = func(key: uint8[list], message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/aes-cmac.nra#L88)
  The 16-byte AES-CMAC tag of the message under the key. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#205-stdcryptomac). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
