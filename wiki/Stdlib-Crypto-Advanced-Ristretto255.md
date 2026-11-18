# `Std.Crypto.Advanced.Ristretto255`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Advanced.Ristretto255` to use this module. The [package specification](Stdlib-Design#2113-stdcryptoadvanced) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ristretto255** · [`export let identity = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L40)
  The canonical 32-byte encoding of the identity. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let generator = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L48)
  The canonical 32-byte encoding of the RFC 9496 generator. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let parse_element = func(bytes: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L56)
  Validates and canonically re-encodes an element. The identity is a valid group element. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let is_identity = func(bytes: uint8[list]) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L64)
  Tests a canonical encoded point for the group identity. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let add = func(left: uint8[list], right: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L72)
  Adds two canonically encoded points. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let subtract = func(left: uint8[list], right: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L82)
  Subtracts two canonically encoded points. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let multiply = func(scalar: uint8[list], point: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L93)
  Multiplies a canonical encoded point by a canonical 32-byte little-endian scalar. The multiplication is branch-free in source. The runtime does not guarantee constant time. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let multiply_base = func(scalar: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L103)
  Multiplies the standardized generator by a canonical 32-byte little-endian scalar. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let from_uniform = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L111)
  Derives a point from exactly 64 bytes as specified in RFC 9496. Another width traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let scalar_reduce = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L119)
  Reduces exactly 64 little-endian bytes modulo the group order. Another width traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let scalar_add = func(left: uint8[list], right: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L130)
  Adds canonically encoded scalars modulo the group order. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let scalar_subtract = func(left: uint8[list], right: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L140)
  Subtracts canonically encoded scalars modulo the group order. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let scalar_multiply = func(left: uint8[list], right: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L150)
  Multiplies canonically encoded scalars modulo the group order. Tagged `#NoDiscard`. Since 0.3.0.
- **Ristretto255** · [`export let scalar_invert = func(scalar: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/ristretto255.nra#L160)
  Inverts a nonzero canonical scalar modulo the group order. Zero returns a Result error. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
