# `Std.Crypto.Asymmetric.X448`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.X448` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **X448** · [`export let X448 = class implements CryptographicPrimitive<X448>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L28)
  The X448 key-agreement primitive, for the policy and for `PrivateKey<X448>`. Tagged `#Final`. Since 0.3.0.
- **X448.X448** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L30)
  Checks the policy.
- **X448.X448** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L35)
  `Algorithm.X448`.
- **X448.X448** · [`export let diffie_hellman = func(private_key: X448PrivateKey, peer: X448PublicKey) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L40)
  The shared secret of the private key and the peer's public key; see `X448PrivateKey.diffie_hellman`. Tagged `#NoDiscard`.
- **X448.X448** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L43)
- **X448** · [`export let X448PublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L48)
  An X448 public key: 56 bytes, the u-coordinate little-endian. Equal keys have equal bytes. Tagged `#Final`. Since 0.3.0.
- **X448.X448PublicKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L52)
  Copies the 56 bytes; another length traps (use `parse_public_key` for outside input).
- **X448.X448PublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L62)
  A copy of the 56 bytes. Tagged `#NoDiscard`.
- **X448.X448PublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L64)
- **X448.X448PublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L71)
- **X448.X448PublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L79)
- **X448** · [`export let X448PrivateKey = class implements PrivateKey<X448>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L85)
  An X448 private key: 56 bytes in secret memory, with its public key. `to_string` never prints it. Tagged `#Final`. Since 0.3.0.
- **X448.X448PrivateKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L90)
  Copies the 56 bytes and computes the public key; another length traps.
- **X448.X448PrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L99)
- **X448.X448PrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L100)
- **X448.X448PrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L101)
- **X448.X448PrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L102)
- **X448.X448PrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L103)
- **X448.X448PrivateKey** · [`export let public_key = func -> X448PublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L107)
  The matching public key. Tagged `#NoDiscard`.
- **X448.X448PrivateKey** · [`export let diffie_hellman = func(peer: X448PublicKey) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L112)
  X448(this key, the peer's key). An all-zero result is `Custom` code `crypto.key_agreement`: the peer sent a small-order point. Tagged `#NoDiscard`.
- **X448.X448PrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L124)
  A fixed placeholder, never the key.
- **X448** · [`export let generate_key_pair = func -> Result<X448PrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L137)
  A new private key from 56 bytes of `Entropy.os_bytes`, or from the given generator. Fails when the entropy source fails. Tagged `#NoDiscard`. Since 0.3.0.
- **X448** · [`export let parse_public_key = func(bytes: uint8[list]) -> Result<X448PublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L157)
  A public key from outside the program. A wrong length is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **X448** · [`export let x448 = func(scalar: uint8[list], u: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x448.nra#L167)
  The X448 function of RFC 7748, section 5, on raw 56-byte strings: the scalar is clamped, the result is returned even when it is all zero. Other lengths trap. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
