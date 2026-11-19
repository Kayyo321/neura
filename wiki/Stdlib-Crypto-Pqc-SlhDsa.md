# `Std.Crypto.Pqc.SlhDsa`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pqc.SlhDsa` to use this module. The [package specification](Stdlib-Design#219-stdcryptopqc) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SlhDsa** · [`export let ParameterSet = enum ( Sha2_128s, Sha2_128f, Sha2_192s, Sha2_192f, Sha2_256s, Sha2_256f, Shake128s, Shake128f, Shake192s, Shake192f, Shake256s, Shake256f )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L31)
  The parameter sets of FIPS 205, section 11. Since 0.3.0.
- **SlhDsa** · [`export let security_bytes = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L48)
  n, the security parameter in bytes: 16, 24 or 32. Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let name = func(set: ParameterSet) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L72)
  The standard's name of the parameter set, such as "SLH-DSA-SHAKE-128f". Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let public_key_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L80)
  The public-key length in bytes, 2n. Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let private_key_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L84)
  The private-key length in bytes, 4n. Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let signature_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L88)
  The signature length in bytes: from 7856 (SLH-DSA-*-128s) to 49856 (SLH-DSA-*-256f). Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let SlhDsaPublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L96)
  An SLH-DSA public key (PK.seed || PK.root). Equal keys have equal bytes. Tagged `#Final`. Since 0.3.0.
- **SlhDsa.SlhDsaPublicKey** · [`export last parameter_set: ParameterSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L98)
  The parameter set.
- **SlhDsa.SlhDsaPublicKey** · [`export let init = func(set: ParameterSet, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L103)
  Copies the encoded key; a wrong length traps with `invalid argument` (use `SlhDsa.parse_public_key` for keys from outside the program).
- **SlhDsa.SlhDsaPublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L115)
  A copy of the encoded key. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaPublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L117)
- **SlhDsa.SlhDsaPublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L133)
- **SlhDsa.SlhDsaPublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L141)
- **SlhDsa** · [`export let SlhDsaPrivateKey = class implements PrivateKey<SlhDsaSigner>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L148)
  An SLH-DSA private key (SK.seed || SK.prf || PK.seed || PK.root) in secret memory. `to_string` never prints it. Tagged `#Final`. Since 0.3.0.
- **SlhDsa.SlhDsaPrivateKey** · [`export last parameter_set: ParameterSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L150)
  The parameter set.
- **SlhDsa.SlhDsaPrivateKey** · [`export let init = func(set: ParameterSet, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L155)
  Copies the encoded key; a wrong length traps with `invalid argument`. PK.root is taken as given: recomputing it costs as much as key generation (use `SlhDsa.key_pair_from_seed` to derive it).
- **SlhDsa.SlhDsaPrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L165)
- **SlhDsa.SlhDsaPrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L166)
- **SlhDsa.SlhDsaPrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L167)
- **SlhDsa.SlhDsaPrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L168)
- **SlhDsa.SlhDsaPrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L171)
  PK.seed || PK.root, the second half of the key.
- **SlhDsa.SlhDsaPrivateKey** · [`export let public_key = func -> SlhDsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L178)
  The matching public key. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaPrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L181)
  A fixed placeholder, never the key.
- **SlhDsa** · [`export let key_pair_from_seed = func(set: ParameterSet, sk_seed: uint8[list], sk_prf: uint8[list], pk_seed: uint8[list]) -> SlhDsaPrivateKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L186)
  slh_keygen_internal (FIPS 205, Algorithm 18) from the three n-byte seeds; another length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let generate_key_pair = func(set: ParameterSet) -> Result<SlhDsaPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L204)
  slh_keygen (FIPS 205, Algorithm 21) with 3n bytes from `Entropy.os_bytes`, or from the given generator. Fails when the entropy source fails. Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let parse_public_key = func(set: ParameterSet, bytes: uint8[list]) -> Result<SlhDsaPublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L228)
  A public key from outside the program. A wrong length is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let parse_private_key = func(set: ParameterSet, bytes: uint8[list]) -> Result<SlhDsaPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L237)
  A private key from outside the program. A wrong length is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **SlhDsa** · [`export let SlhDsaSigner = class implements Signer<SlhDsaPrivateKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L246)
  Signs with an SLH-DSA private key. `sign` is hedged pure SLH-DSA with an empty context. Tagged `#Final`. Since 0.3.0.
- **SlhDsa.SlhDsaSigner** · [`export let init = func(private_key: SlhDsaPrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L253)
  A signer over the key, with `addrnd` from `Entropy.os_bytes` or from the given generator.
- **SlhDsa.SlhDsaSigner** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L266)
  `SlhDsa`.
- **SlhDsa.SlhDsaSigner** · [`export let sign_internal = func(message_prime: uint8[list], addrnd: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L287)
  slh_sign_internal (FIPS 205, Algorithm 19) over a formatted message M' with the given n-byte addrnd (PK.seed for the deterministic variant). Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaSigner** · [`export let sign_with = func(message: uint8[list], context: uint8[list], addrnd: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L294)
  slh_sign (FIPS 205, Algorithm 22) with the context string and the given n-byte addrnd. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaSigner** · [`export let sign_deterministic = func(message: uint8[list], context: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L301)
  The deterministic variant of slh_sign: addrnd is PK.seed. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaSigner** · [`export let sign_prehash_with = func(message: uint8[list], context: uint8[list], hash: Algorithm, addrnd: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L309)
  hash_slh_sign (FIPS 205, Algorithm 23) with the pre-hash function `hash` (one of the SHA-2, SHA-3 and SHAKE algorithms of Std.Crypto.Hash) and the given n-byte addrnd. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaSigner** · [`export let sign_prehash_deterministic = func(message: uint8[list], context: uint8[list], hash: Algorithm) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L317)
  The deterministic variant of hash_slh_sign. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaSigner** · [`export let try_sign = func(message: uint8[list], context: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L325)
  Hedged slh_sign with the context string. Fails when the entropy source fails. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaSigner** · [`export let try_sign_prehash = func(message: uint8[list], context: uint8[list], hash: Algorithm) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L338)
  Hedged hash_slh_sign. Fails when the entropy source fails. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaSigner** · [`export let sign = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L351)
  Hedged pure SLH-DSA with an empty context. Traps with `unavailable: ...` when the entropy source fails; `try_sign` returns that failure instead.
- **SlhDsa.SlhDsaSigner** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L362)
- **SlhDsa** · [`export let SlhDsaVerifier = class implements Verifier<SlhDsaPublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L368)
  Verifies SLH-DSA signatures under a public key. `verify` is pure SLH-DSA with an empty context. A signature of the wrong length, a context longer than 255 bytes, or a failed check gives false. Tagged `#Final`. Since 0.3.0.
- **SlhDsa.SlhDsaVerifier** · [`export let init = func(public_key: SlhDsaPublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L373)
  A verifier for the public key.
- **SlhDsa.SlhDsaVerifier** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L380)
  `SlhDsa`.
- **SlhDsa.SlhDsaVerifier** · [`export let public_key = func -> SlhDsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L382)
- **SlhDsa.SlhDsaVerifier** · [`export let verify_internal = func(message_prime: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L386)
  slh_verify_internal (FIPS 205, Algorithm 20) over a formatted message M'. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaVerifier** · [`export let verify_with_context = func(message: uint8[list], signature: uint8[list], context: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L395)
  slh_verify (FIPS 205, Algorithm 24) with the context string. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaVerifier** · [`export let verify_prehash = func(message: uint8[list], signature: uint8[list], context: uint8[list], hash: Algorithm) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L404)
  hash_slh_verify (FIPS 205, Algorithm 25) with the pre-hash function `hash`. Tagged `#NoDiscard`.
- **SlhDsa.SlhDsaVerifier** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L412)
- **SlhDsa.SlhDsaVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/slh-dsa.nra#L415)
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
