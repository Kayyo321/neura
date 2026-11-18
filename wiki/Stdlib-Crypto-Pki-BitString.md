# `Std.Crypto.Pki.BitString`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.BitString` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **BitString** · [`export let BitString = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L8)
  A bit string: whole bytes, of which the last `unused_bits` low-order bits are not part of the value and are zero. Bit 0 is the most significant bit of the first byte, as ASN.1 numbers named bits. Tagged `#Final`. Since 0.3.0.
- **BitString.BitString** · [`export last bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L10)
  The bytes. Do not change the list.
- **BitString.BitString** · [`export last unused_bits: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L12)
  The number of unused bits in the last byte, 0 to 7 (0 for an empty string).
- **BitString.BitString** · [`export let init = func(value: uint8[list], unused: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L16)
  A copy of the bytes with the count of unused bits. An unused count outside 0 to 7, a nonzero count on an empty string, or a set unused bit traps with "invalid argument: BIT STRING ...".
- **BitString.BitString** · [`export let bit_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L32)
  The number of bits in the value.
- **BitString.BitString** · [`export let bit = func(index: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L35)
  Whether bit `index` is set; a bit past the end reads as unset.
- **BitString.BitString** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L42)
- **BitString.BitString** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L57)
- **BitString.BitString** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/bit-string.nra#L66)
  "BitString(<bits> bits)".
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
