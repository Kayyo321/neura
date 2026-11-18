# `Std.Crypto.Advanced.HashToCurve`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Advanced.HashToCurve` to use this module. The [package specification](Stdlib-Design#2113-stdcryptoadvanced) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/hash-to-curve.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **HashToCurve** · [`export let expand_message_xmd_sha512 = func(message: uint8[list], dst: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/hash-to-curve.nra#L16)
  Expands with SHA-512 and the supplied domain tag, including hashed tags longer than 255 bytes. An empty domain tag or an output length outside 0 through 16,320 bytes traps. Tagged `#NoDiscard`. Since 0.3.0.
- **HashToCurve** · [`export let hash_to_ristretto255 = func(message: uint8[list], dst: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/hash-to-curve.nra#L23)
  The 32-byte encoding of a point derived with ristretto255_XMD:SHA-512_R255MAP_RO_. The caller supplies the protocol's nonempty domain separation tag. Tagged `#NoDiscard`. Since 0.3.0.
- **HashToCurve** · [`export let hash_to_p256 = func(message: uint8[list], dst: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/hash-to-curve.nra#L33)
  The RFC 9380 P256_XMD:SHA-256_SSWU_RO_ suite with the protocol's nonempty domain separation tag. Returns SEC1 uncompressed by default or compressed when requested. The identity is encoded as 0x00. An empty domain tag traps. Tagged `#NoDiscard`. Since 0.3.0.
- **HashToCurve** · [`export let hash_to_p384 = func(message: uint8[list], dst: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/hash-to-curve.nra#L43)
  The RFC 9380 P384_XMD:SHA-384_SSWU_RO_ suite with the protocol's nonempty domain separation tag. Returns SEC1 uncompressed by default or compressed when requested. The identity is encoded as 0x00. An empty domain tag traps. Tagged `#NoDiscard`. Since 0.3.0.
- **HashToCurve** · [`export let hash_to_p521 = func(message: uint8[list], dst: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/hash-to-curve.nra#L53)
  The RFC 9380 P521_XMD:SHA-512_SSWU_RO_ suite with the protocol's nonempty domain separation tag. Returns SEC1 uncompressed by default or compressed when requested. The identity is encoded as 0x00. An empty domain tag traps. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
