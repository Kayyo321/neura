# `Std.Crypto.Asymmetric.X25519`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.X25519` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **X25519** · [`export let X25519 = class implements CryptographicPrimitive<X25519>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L28)
  The X25519 key-agreement primitive, for the policy and for `PrivateKey<X25519>`. Tagged `#Final`. Since 0.3.0.
- **X25519.X25519** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L30)
  Checks the policy.
- **X25519.X25519** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L35)
  `Algorithm.X25519`.
- **X25519.X25519** · [`export let diffie_hellman = func(private_key: X25519PrivateKey, peer: X25519PublicKey) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L40)
  The shared secret of the private key and the peer's public key; see `X25519PrivateKey.diffie_hellman`. Tagged `#NoDiscard`.
- **X25519.X25519** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L43)
- **X25519** · [`export let X25519PublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L48)
  An X25519 public key: 32 bytes, the u-coordinate little-endian. Equal keys have equal bytes. Tagged `#Final`. Since 0.3.0.
- **X25519.X25519PublicKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L52)
  Copies the 32 bytes; another length traps (use `parse_public_key` for outside input).
- **X25519.X25519PublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L62)
  A copy of the 32 bytes. Tagged `#NoDiscard`.
- **X25519.X25519PublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L64)
- **X25519.X25519PublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L71)
- **X25519.X25519PublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L79)
- **X25519** · [`export let X25519PrivateKey = class implements PrivateKey<X25519>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L85)
  An X25519 private key: 32 bytes in secret memory, with its public key. `to_string` never prints it. Tagged `#Final`. Since 0.3.0.
- **X25519.X25519PrivateKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L90)
  Copies the 32 bytes and computes the public key; another length traps.
- **X25519.X25519PrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L99)
- **X25519.X25519PrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L100)
- **X25519.X25519PrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L101)
- **X25519.X25519PrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L102)
- **X25519.X25519PrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L103)
- **X25519.X25519PrivateKey** · [`export let public_key = func -> X25519PublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L107)
  The matching public key. Tagged `#NoDiscard`.
- **X25519.X25519PrivateKey** · [`export let diffie_hellman = func(peer: X25519PublicKey) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L112)
  X25519(this key, the peer's key). An all-zero result is `Custom` code `crypto.key_agreement`: the peer sent a small-order point. Tagged `#NoDiscard`.
- **X25519.X25519PrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L124)
  A fixed placeholder, never the key.
- **X25519** · [`export let generate_key_pair = func -> Result<X25519PrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L137)
  A new private key from 32 bytes of `Entropy.os_bytes`, or from the given generator. Fails when the entropy source fails. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519** · [`export let parse_public_key = func(bytes: uint8[list]) -> Result<X25519PublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L157)
  A public key from outside the program. A wrong length is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519** · [`export let x25519 = func(scalar: uint8[list], u: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/x25519.nra#L167)
  The X25519 function of RFC 7748, section 5, on raw 32-byte strings: the scalar is clamped, the result is returned even when it is all zero. Other lengths trap. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
