# `Std.Crypto.Advanced.Sss`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Advanced.Sss` to use this module. The [package specification](Stdlib-Design#2113-stdcryptoadvanced) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Sss** · [`export const PrimeCoefficientBytes = 82`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L33)
  The random bytes a prime-field coefficient takes. Since 0.3.0.
- **Sss** · [`export const PrimeValueBytes = 66`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L38)
  The byte length of a prime-field share value. Since 0.3.0.
- **Sss** · [`export const PrimeMaxSecretBytes = 65`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L43)
  The longest secret the prime field takes. Since 0.3.0.
- **Sss** · [`export let Gf256Share = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L49)
  A GF(2^8) share: the x-coordinate (1 to 255) and one y byte per secret byte, in secret memory. `to_string` never prints the y bytes. Equality is identity, as for `SecretBytes`. Tagged `#Final`. Since 0.3.0.
- **Sss.Gf256Share** · [`export last x: uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L53)
  The x-coordinate, from 1 to 255. Since 0.3.0.
- **Sss.Gf256Share** · [`export let init = func(share_x: uint8, y: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L59)
  A share from its x-coordinate and y bytes; x = 0 or no y bytes trap. Since 0.3.0.
- **Sss.Gf256Share** · [`export let length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L71)
  The number of y bytes, which is the secret's length. Since 0.3.0.
- **Sss.Gf256Share** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L77)
  The y bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss.Gf256Share** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L83)
  Vault's layout: the y bytes followed by the x byte. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss.Gf256Share** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L92)
  Overwrites the y bytes. Since 0.3.0.
- **Sss.Gf256Share** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L97)
  A fixed display that redacts the share. Since 0.3.0.
- **Sss** · [`export let PrimeShare = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L104)
  A prime-field share: the x-coordinate (at least 1) and the 66-byte big-endian value f(x) below 2^521 - 1, in secret memory. `to_string` never prints the value. Equality is identity. Tagged `#Final`. Since 0.3.0.
- **Sss.PrimeShare** · [`export last x: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L108)
  The x-coordinate, at least 1. Since 0.3.0.
- **Sss.PrimeShare** · [`export let init = func(share_x: int, y: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L115)
  A share from its x-coordinate and 66-byte value; x below 1, another length or a value not below p trap (use `parse_prime_share` for outside input). Since 0.3.0.
- **Sss.PrimeShare** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L128)
  The 66 value bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss.PrimeShare** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L134)
  x as 4 big-endian bytes followed by the 66 value bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss.PrimeShare** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L144)
  Overwrites the value bytes. Since 0.3.0.
- **Sss.PrimeShare** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L149)
  A fixed display that redacts the share. Since 0.3.0.
- **Sss** · [`export let split_with_coefficients = func(secret: uint8[list], parts: int, threshold: int, coefficients: uint8[list]) -> Gf256Share[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L220)
  The GF(2^8) shares of the secret for x = 1 to `parts`, with the given coefficients: for each secret byte in order, the coefficients a_1 to a_(threshold-1) (secret length times threshold - 1 bytes, as Vault draws them). A wrong coefficient length, an empty secret, or counts outside 2 <= threshold <= parts <= 255 trap. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss** · [`export let split = func(secret: uint8[list], parts: int, threshold: int) -> Result<Gf256Share[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L255)
  The GF(2^8) shares with coefficients from `Entropy.os_bytes`; fails when the entropy source fails. The same traps as `split_with_coefficients`. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss** · [`export let parse_share = func(bytes: uint8[list]) -> Result<Gf256Share>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L281)
  A GF(2^8) share from Vault's layout (y bytes, then x). Fewer than 2 bytes or x = 0 is `Custom` code `crypto.invalid_share`. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss** · [`export let combine = func(shares: Gf256Share[list]) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L294)
  The secret rebuilt from GF(2^8) shares by Lagrange interpolation at 0. Fewer than two shares (or fewer than `threshold`), shares of different lengths, or a repeated x-coordinate is `Custom` code `crypto.invalid_share`. Shares beyond the threshold take part in the interpolation; a wrong share gives a wrong secret. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss** · [`export let split_prime_with_coefficients = func(secret: uint8[list], parts: int, threshold: int, random: uint8[list]) -> PrimeShare[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L347)
  The prime-field shares of the secret (1 to 65 bytes, read as a big-endian integer) for x = 1 to `parts`, with the given random bytes: 82 bytes per coefficient a_1 to a_(threshold-1), each read big-endian and reduced modulo p. A wrong length or counts outside 2 <= threshold <= parts <= 65535 trap. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss** · [`export let split_prime = func(secret: uint8[list], parts: int, threshold: int) -> Result<PrimeShare[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L384)
  The prime-field shares with random bytes from `Entropy.os_bytes` or the given generator; the same traps as `split_prime_with_coefficients`. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss** · [`export let parse_prime_share = func(bytes: uint8[list]) -> Result<PrimeShare>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L412)
  A prime-field share from `PrimeShare.to_bytes`'s 70 bytes. Another length, x = 0 or above 2^31 - 1, or a value not below p is `Custom` code `crypto.invalid_share`. Tagged `#NoDiscard`. Since 0.3.0.
- **Sss** · [`export let combine_prime = func(shares: PrimeShare[list], secret_length: int) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/sss.nra#L429)
  The secret of `secret_length` bytes (1 to 65) rebuilt from prime-field shares by Lagrange interpolation at 0. Too few shares, a repeated x-coordinate, or a result that does not fit in `secret_length` bytes (shares that do not belong together) is `Custom` code `crypto.invalid_share`. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
