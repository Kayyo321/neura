# `Std.Crypto.Hash.Blake3`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Hash.Blake3`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Hash.Blake3`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Blake3** · [`export let Blake3 = class implements ExtendableOutput<Blake3>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L161)
  A running BLAKE3 hash in one of its three modes. `finish` returns 32 bytes; `squeeze` and `read` continue the output stream. Tagged `#Final`. Since 0.3.0.
- **Blake3.Blake3** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L176)
  The plain hash; with a 32-byte key, the keyed hash (any other key length traps).
- **Blake3.Blake3** · [`export let start = func(key_value: uint32[list], mode: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L187)
  A hasher with the key words and mode flags: the start of every mode.
- **Blake3.Blake3** · [`export static let create = func() -> Blake3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L195)
  A new plain hasher.
- **Blake3.Blake3** · [`export static let output_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L197)
  32.
- **Blake3.Blake3** · [`export static let block_length = func() -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L199)
  64.
- **Blake3.Blake3** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L201)
  `Algorithm.Blake3`.
- **Blake3.Blake3** · [`export let update = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L204)
  Adds the bytes. Traps with "invalid state: digest finished" once output was read.
- **Blake3.Blake3** · [`export let finish = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L258)
  The first 32 bytes of output (or the next 32, if output was already read).
- **Blake3.Blake3** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L261)
  The next `count` bytes of output.
- **Blake3.Blake3** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L283)
- **Blake3** · [`export let hash = func(bytes: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L288)
  The first `length` bytes of the BLAKE3 hash of the bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Blake3** · [`export let keyed_hash = func(key: uint8[list], bytes: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L296)
  The first `length` bytes of the keyed hash under a 32-byte key. Tagged `#NoDiscard`. Since 0.3.0.
- **Blake3** · [`export let derive_key = func(context: string, material: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/blake3.nra#L305)
  `length` bytes of key material derived from `material` under the context string, which should be a hardcoded, globally unique, application-specific text. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#214-stdcryptohash). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
