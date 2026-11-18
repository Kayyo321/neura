# `Std.Crypto.Symmetric.Ff1`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Symmetric.Ff1`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Symmetric.Ff1`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ff1** · [`export let Ff1 = class implements CryptographicPrimitive<Ff1>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L27)
  FF1 under an AES key (16, 24 or 32 bytes) for one radix. Tagged `#Final`. Since 0.3.0.
- **Ff1.Ff1** · [`export last radix: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L30)
  The radix, from 2 to 65536.
- **Ff1.Ff1** · [`export last min_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L32)
  The shortest accepted numeral string.
- **Ff1.Ff1** · [`export let init = func(key: uint8[list], numeral_radix: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L35)
  Keys FF1 for the radix. A radix outside 2 to 65536, or a key of another length, traps.
- **Ff1.Ff1** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L52)
  `Algorithm.Ff1`.
- **Ff1.Ff1** · [`export let encrypt = func(numerals: int[list], tweak: uint8[list]) -> int[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L57)
  FF1.Encrypt of the numerals (each below the radix) under the tweak. A numeral out of range or a string shorter than `min_length` traps. Tagged `#NoDiscard`.
- **Ff1.Ff1** · [`export let decrypt = func(numerals: int[list], tweak: uint8[list]) -> int[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L61)
  FF1.Decrypt, the inverse of `encrypt`. Tagged `#NoDiscard`.
- **Ff1.Ff1** · [`export let encrypt_text = func(text: string, tweak: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L67)
  `encrypt` over text in the alphabet 0-9a-z cut to the radix (radix 36 at most). A character outside it fails with an Encoding error at its offset; uppercase letters are accepted and the result is lowercase. Tagged `#NoDiscard`.
- **Ff1.Ff1** · [`export let decrypt_text = func(text: string, tweak: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L71)
  `decrypt` over text, the inverse of `encrypt_text`. Tagged `#NoDiscard`.
- **Ff1.Ff1** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/symmetric/ff1.nra#L217)
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#217-stdcryptosymmetric). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
