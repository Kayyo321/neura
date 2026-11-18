# `Std.Crypto.Asymmetric.Rsa`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.Rsa` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Rsa** · [`export let RsaPublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L74)
  An RSA public key (n, e). Equal keys have equal numbers. Tagged `#Final`. Since 0.3.0.
- **Rsa.RsaPublicKey** · [`export last core: rsa_core.PublicCore`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L76)
  The numbers and their arithmetic (library use).
- **Rsa.RsaPublicKey** · [`export let init = func(modulus: uint8[list], exponent: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L80)
  The key of the big-endian n and e. A modulus outside 1024 to 16384 bits, an even modulus, or an exponent that is even, below 3 or not below n traps (use `parse_public_key` for outside input).
- **Rsa.RsaPublicKey** · [`export let bits = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L90)
  The bit length of n.
- **Rsa.RsaPublicKey** · [`export let modulus = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L94)
  n as big-endian bytes without leading zeros. Tagged `#NoDiscard`.
- **Rsa.RsaPublicKey** · [`export let exponent = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L98)
  e as big-endian bytes without leading zeros. Tagged `#NoDiscard`.
- **Rsa.RsaPublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L100)
- **Rsa.RsaPublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L108)
- **Rsa.RsaPublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L116)
- **Rsa** · [`export let RsaPrivateKey = class implements PrivateKey<RsaPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L123)
  An RSA private key in CRT form. `expose` gives the concatenated big-endian numbers d, p, q, dP, dQ and qInv, each of n's byte length (or half of it for the last five), in secret memory. `to_string` never prints them. Tagged `#Final`. Since 0.3.0.
- **Rsa.RsaPrivateKey** · [`export last public_key: RsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L125)
  The public key.
- **Rsa.RsaPrivateKey** · [`export last core: rsa_core.PrivateCore`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L127)
  The CRT arithmetic (library use).
- **Rsa.RsaPrivateKey** · [`export let init = func(modulus: uint8[list], exponent: uint8[list], private_exponent: uint8[list], prime_p: uint8[list], prime_q: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L132)
  The key of n, e, d, p and q (big-endian); dP, dQ and qInv are computed. Numbers that are not a consistent key trap (use `parse_private_key` for outside input).
- **Rsa.RsaPrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L159)
- **Rsa.RsaPrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L162)
  Disables private operations and zeros the serialized secret and the directly held CRT buffers. Already-created signers and decryptors share this state. The runtime does not guarantee memory erasure.
- **Rsa.RsaPrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L167)
- **Rsa.RsaPrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L168)
- **Rsa.RsaPrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L171)
  n || e as big-endian bytes, n padded to its byte length (the public numbers are not secret).
- **Rsa.RsaPrivateKey** · [`export let bits = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L178)
  The bit length of n.
- **Rsa.RsaPrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L181)
  A fixed placeholder with the size, never a number.
- **Rsa** · [`export let parse_public_key = func(modulus: uint8[list], exponent: uint8[list]) -> Result<RsaPublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L272)
  A public key from outside the program. Unusable numbers are `Custom` code `crypto.invalid_key`; a modulus below 2048 bits under a policy that denies `Rsa1024` is `Custom` code `crypto.policy`. Tagged `#NoDiscard`. Since 0.3.0.
- **Rsa** · [`export let parse_private_key = func(modulus: uint8[list], exponent: uint8[list], private_exponent: uint8[list], prime_p: uint8[list], prime_q: uint8[list], exponent_p: uint8[list], exponent_q: uint8[list], coefficient: uint8[list]) -> Result<RsaPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L290)
  A private key from outside the program: n, e, d, p, q and the optional dP, dQ, qInv (pass empty lists to compute them). Numbers that are not a consistent key are `Custom` code `crypto.invalid_key`; a modulus below 2048 bits under a policy that denies `Rsa1024` is `Custom` code `crypto.policy`. Tagged `#NoDiscard`. Since 0.3.0.
- **Rsa** · [`export let generate_key_pair = func(bits: int) -> Result<RsaPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa.nra#L524)
  A new key of `bits` bits (2048, 3072 or 4096; another size traps) with e = 65537, from `Entropy.os_bytes` or from the given generator (deterministic for a seeded one). Fails when the entropy source fails, or with `Custom` code `crypto.key_generation` after FIPS 186-5's bound on attempts. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
