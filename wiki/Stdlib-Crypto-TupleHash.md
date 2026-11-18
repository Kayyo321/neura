# `Std.Crypto.Hash.TupleHash`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Hash.TupleHash`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Hash.TupleHash`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TupleHash** · [`export let TupleHash128 = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L20)
  A running TupleHash128: `add` each item, then `finish(length)` for TupleHash128 or `finish_xof(length)` and `squeeze` for TupleHashXOF128. A second finish, or an `add` after one, traps. Tagged `#Final`. Since 0.3.0.
- **TupleHash.TupleHash128** · [`export let init = func(customization: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L25)
  An empty tuple with the customization string S.
- **TupleHash.TupleHash128** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L31)
  `Algorithm.TupleHash128`.
- **TupleHash.TupleHash128** · [`export let add = func(item: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L34)
  Appends one item to the tuple.
- **TupleHash.TupleHash128** · [`export let finish = func(length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L39)
  TupleHash128 with an output of `length` bytes.
- **TupleHash.TupleHash128** · [`export let finish_xof = func(length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L45)
  The first `length` bytes of TupleHashXOF128; `squeeze` continues.
- **TupleHash.TupleHash128** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L51)
  The next `count` bytes after `finish_xof`.
- **TupleHash.TupleHash128** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L53)
- **TupleHash** · [`export let tuple_hash128 = func(items: uint8[list][list], length: int, customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L58)
  TupleHash128(items, length, customization). Tagged `#NoDiscard`. Since 0.3.0.
- **TupleHash** · [`export let tuple_hash128_xof = func(items: uint8[list][list], length: int, customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L68)
  The first `length` bytes of TupleHashXOF128(items, customization). Tagged `#NoDiscard`. Since 0.3.0.
- **TupleHash** · [`export let TupleHash256 = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L80)
  A running TupleHash256: `add` each item, then `finish(length)` for TupleHash256 or `finish_xof(length)` and `squeeze` for TupleHashXOF256. A second finish, or an `add` after one, traps. Tagged `#Final`. Since 0.3.0.
- **TupleHash.TupleHash256** · [`export let init = func(customization: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L85)
  An empty tuple with the customization string S.
- **TupleHash.TupleHash256** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L91)
  `Algorithm.TupleHash256`.
- **TupleHash.TupleHash256** · [`export let add = func(item: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L94)
  Appends one item to the tuple.
- **TupleHash.TupleHash256** · [`export let finish = func(length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L99)
  TupleHash256 with an output of `length` bytes.
- **TupleHash.TupleHash256** · [`export let finish_xof = func(length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L105)
  The first `length` bytes of TupleHashXOF256; `squeeze` continues.
- **TupleHash.TupleHash256** · [`export let squeeze = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L111)
  The next `count` bytes after `finish_xof`.
- **TupleHash.TupleHash256** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L113)
- **TupleHash** · [`export let tuple_hash256 = func(items: uint8[list][list], length: int, customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L118)
  TupleHash256(items, length, customization). Tagged `#NoDiscard`. Since 0.3.0.
- **TupleHash** · [`export let tuple_hash256_xof = func(items: uint8[list][list], length: int, customization: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/hash/tuple-hash.nra#L128)
  The first `length` bytes of TupleHashXOF256(items, customization). Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#204-stdcryptohash). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
