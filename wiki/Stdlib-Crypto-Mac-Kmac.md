# `Std.Crypto.Mac.Kmac`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Mac.Kmac`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Mac.Kmac`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Kmac** · [`export let Kmac128 = class implements Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L26)
  KMAC128 with a tag of a length fixed at construction (32 bytes by default for KMAC128, 64 for KMAC256, or any length of at least 4). `finish_xof(length)` and `squeeze` give KMACXOF128 instead. Tagged `#Final`. Since 0.3.0.
- **Kmac.Kmac128** · [`export let init = func(key: uint8[list], customization: uint8[list], output_length: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L32)
  KMAC128 under the key with customization string S and a tag of `output_length` bytes.
- **Kmac.Kmac128** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L46)
  `Algorithm.Kmac128`.
- **Kmac.Kmac128** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L47)
- **Kmac.Kmac128** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L49)
- **Kmac.Kmac128** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L56)
- **Kmac.Kmac128** · [`export let finish_xof = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L66)
  The first `count` bytes of KMACXOF128; `squeeze` continues. Ends the MAC like `finish`.
- **Kmac.Kmac128** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L76)
  The next `count` bytes of KMACXOF128 output after `finish_xof`.
- **Kmac.Kmac128** · [`export let verify = func(tag: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L83)
- **Kmac.Kmac128** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L85)
- **Kmac** · [`export let kmac128 = func(key: uint8[list], data: uint8[list], length: int, customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L90)
  KMAC128(key, data, length, customization). Tagged `#NoDiscard`. Since 0.3.0.
- **Kmac** · [`export let kmac128_xof = func(key: uint8[list], data: uint8[list], length: int, customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L98)
  The first `length` bytes of KMACXOF128(key, data, customization). Tagged `#NoDiscard`. Since 0.3.0.
- **Kmac** · [`export let Kmac256 = class implements Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L108)
  KMAC256 with a tag of a length fixed at construction (32 bytes by default for KMAC128, 64 for KMAC256, or any length of at least 4). `finish_xof(length)` and `squeeze` give KMACXOF256 instead. Tagged `#Final`. Since 0.3.0.
- **Kmac.Kmac256** · [`export let init = func(key: uint8[list], customization: uint8[list], output_length: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L114)
  KMAC256 under the key with customization string S and a tag of `output_length` bytes.
- **Kmac.Kmac256** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L128)
  `Algorithm.Kmac256`.
- **Kmac.Kmac256** · [`export let tag_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L129)
- **Kmac.Kmac256** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L131)
- **Kmac.Kmac256** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L138)
- **Kmac.Kmac256** · [`export let finish_xof = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L148)
  The first `count` bytes of KMACXOF256; `squeeze` continues. Ends the MAC like `finish`.
- **Kmac.Kmac256** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L158)
  The next `count` bytes of KMACXOF256 output after `finish_xof`.
- **Kmac.Kmac256** · [`export let verify = func(tag: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L165)
- **Kmac.Kmac256** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L167)
- **Kmac** · [`export let kmac256 = func(key: uint8[list], data: uint8[list], length: int, customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L172)
  KMAC256(key, data, length, customization). Tagged `#NoDiscard`. Since 0.3.0.
- **Kmac** · [`export let kmac256_xof = func(key: uint8[list], data: uint8[list], length: int, customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/mac/kmac.nra#L180)
  The first `length` bytes of KMACXOF256(key, data, customization). Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#215-stdcryptomac). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
