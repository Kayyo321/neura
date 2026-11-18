# `Std.Crypto.Pqc.MlDsa`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pqc.MlDsa` to use this module. The [package specification](Stdlib-Design#219-stdcryptopqc) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MlDsa** · [`export let ParameterSet = enum (MlDsa44, MlDsa65, MlDsa87)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L33)
  The parameter sets of FIPS 204, section 4: ML-DSA-44, ML-DSA-65 and ML-DSA-87. Since 0.3.0.
- **MlDsa** · [`export let name = func(set: ParameterSet) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L49)
  The standard's name of the parameter set, such as "ML-DSA-65". Tagged `#NoDiscard`. Since 0.3.0.
- **MlDsa** · [`export let public_key_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L53)
  The public-key length in bytes: 1312, 1952 or 2592. Tagged `#NoDiscard`. Since 0.3.0.
- **MlDsa** · [`export let private_key_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L61)
  The private-key length in bytes: 2560, 4032 or 4896. Tagged `#NoDiscard`. Since 0.3.0.
- **MlDsa** · [`export let signature_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L69)
  The signature length in bytes: 2420, 3309 or 4627. Tagged `#NoDiscard`. Since 0.3.0.
- **MlDsa** · [`export let MlDsaPublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L79)
  An ML-DSA public key (rho || t1). Equal keys have equal bytes. Tagged `#Final`. Since 0.3.0.
- **MlDsa.MlDsaPublicKey** · [`export last parameter_set: ParameterSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L81)
  The parameter set.
- **MlDsa.MlDsaPublicKey** · [`export let init = func(set: ParameterSet, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L86)
  Copies the encoded key; a wrong length traps with `invalid argument` (use `MlDsa.parse_public_key` for keys from outside the program).
- **MlDsa.MlDsaPublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L98)
  A copy of the encoded key. Tagged `#NoDiscard`.
- **MlDsa.MlDsaPublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L100)
- **MlDsa.MlDsaPublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L116)
- **MlDsa.MlDsaPublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L124)
- **MlDsa** · [`export let MlDsaPrivateKey = class implements PrivateKey<MlDsaSigner>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L131)
  An ML-DSA private key (FIPS 204 skEncode) in secret memory, with its public key. `to_string` never prints it. Tagged `#Final`. Since 0.3.0.
- **MlDsa.MlDsaPrivateKey** · [`export last parameter_set: ParameterSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L133)
  The parameter set.
- **MlDsa.MlDsaPrivateKey** · [`export let init = func(set: ParameterSet, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L140)
  Copies the encoded private key and recomputes its public key. A wrong length, or a key whose s1, s2, tr or t0 is inconsistent, traps with `invalid argument`; use `MlDsa.parse_private_key` for keys from outside the program. The second form takes the public key too and checks only both lengths.
- **MlDsa.MlDsaPrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L165)
- **MlDsa.MlDsaPrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L166)
- **MlDsa.MlDsaPrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L167)
- **MlDsa.MlDsaPrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L168)
- **MlDsa.MlDsaPrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L169)
- **MlDsa.MlDsaPrivateKey** · [`export let public_key = func -> MlDsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L173)
  The matching public key. Tagged `#NoDiscard`.
- **MlDsa.MlDsaPrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L176)
  A fixed placeholder, never the key.
- **MlDsa** · [`export let key_pair_from_seed = func(set: ParameterSet, xi: uint8[list]) -> MlDsaPrivateKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L181)
  ML-DSA.KeyGen_internal (FIPS 204, Algorithm 6) from the 32-byte seed xi; another length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **MlDsa** · [`export let generate_key_pair = func(set: ParameterSet) -> Result<MlDsaPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L195)
  ML-DSA.KeyGen (FIPS 204, Algorithm 1) with a seed from `Entropy.os_bytes`, or from the given generator. Fails when the entropy source fails. Tagged `#NoDiscard`. Since 0.3.0.
- **MlDsa** · [`export let parse_public_key = func(set: ParameterSet, bytes: uint8[list]) -> Result<MlDsaPublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L215)
  A public key from outside the program. A wrong length is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **MlDsa** · [`export let parse_private_key = func(set: ParameterSet, bytes: uint8[list]) -> Result<MlDsaPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L225)
  A private key from outside the program, checked as the constructor does. A failure is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **MlDsa** · [`export let MlDsaSigner = class implements Signer<MlDsaPrivateKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L239)
  Signs with an ML-DSA private key. `sign` is hedged pure ML-DSA with an empty context. Tagged `#Final`. Since 0.3.0.
- **MlDsa.MlDsaSigner** · [`export let init = func(private_key: MlDsaPrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L246)
  A signer over the key, with `rnd` from `Entropy.os_bytes` or from the given generator.
- **MlDsa.MlDsaSigner** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L259)
  `MlDsa44`, `MlDsa65` or `MlDsa87`.
- **MlDsa.MlDsaSigner** · [`export let sign_internal = func(message_prime: uint8[list], rnd: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L280)
  ML-DSA.Sign_internal (FIPS 204, Algorithm 7) over a formatted message M' with the given 32-byte rnd. Tagged `#NoDiscard`.
- **MlDsa.MlDsaSigner** · [`export let sign_mu = func(mu: uint8[list], rnd: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L289)
  ML-DSA.Sign_internal with the 64-byte message representative mu computed elsewhere (the "external mu" form) and the given 32-byte rnd. Tagged `#NoDiscard`.
- **MlDsa.MlDsaSigner** · [`export let sign_with = func(message: uint8[list], context: uint8[list], rnd: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L300)
  ML-DSA.Sign (FIPS 204, Algorithm 2) with the context string and the given 32-byte rnd; 32 zero bytes give the deterministic variant. Tagged `#NoDiscard`.
- **MlDsa.MlDsaSigner** · [`export let sign_deterministic = func(message: uint8[list], context: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L307)
  The deterministic variant of ML-DSA.Sign: rnd is 32 zero bytes. Tagged `#NoDiscard`.
- **MlDsa.MlDsaSigner** · [`export let sign_prehash_with = func(message: uint8[list], context: uint8[list], hash: Algorithm, rnd: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L313)
  HashML-DSA.Sign (FIPS 204, Algorithm 4) with the pre-hash function `hash` (one of the SHA-2, SHA-3 and SHAKE algorithms of Std.Crypto.Hash) and the given 32-byte rnd. Tagged `#NoDiscard`.
- **MlDsa.MlDsaSigner** · [`export let try_sign = func(message: uint8[list], context: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L321)
  Hedged ML-DSA.Sign with the context string. Fails when the entropy source fails. Tagged `#NoDiscard`.
- **MlDsa.MlDsaSigner** · [`export let try_sign_prehash = func(message: uint8[list], context: uint8[list], hash: Algorithm) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L334)
  Hedged HashML-DSA.Sign. Fails when the entropy source fails. Tagged `#NoDiscard`.
- **MlDsa.MlDsaSigner** · [`export let sign = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L347)
  Hedged pure ML-DSA with an empty context. Traps with `unavailable: ...` when the entropy source fails; `try_sign` returns that failure instead.
- **MlDsa.MlDsaSigner** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L358)
- **MlDsa** · [`export let MlDsaVerifier = class implements Verifier<MlDsaPublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L365)
  Verifies ML-DSA signatures under a public key. `verify` is pure ML-DSA with an empty context. A signature of the wrong length, a malformed hint, a context longer than 255 bytes, or any failed check of FIPS 204, Algorithm 8, gives false. Tagged `#Final`. Since 0.3.0.
- **MlDsa.MlDsaVerifier** · [`export let init = func(public_key: MlDsaPublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L371)
  A verifier for the public key.
- **MlDsa.MlDsaVerifier** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L379)
  `MlDsa44`, `MlDsa65` or `MlDsa87`.
- **MlDsa.MlDsaVerifier** · [`export let public_key = func -> MlDsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L381)
- **MlDsa.MlDsaVerifier** · [`export let verify_mu = func(mu: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L385)
  ML-DSA.Verify_internal (FIPS 204, Algorithm 8) with the 64-byte message representative mu. Tagged `#NoDiscard`.
- **MlDsa.MlDsaVerifier** · [`export let verify_internal = func(message_prime: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L397)
  ML-DSA.Verify_internal over a formatted message M'. Tagged `#NoDiscard`.
- **MlDsa.MlDsaVerifier** · [`export let verify_with_context = func(message: uint8[list], signature: uint8[list], context: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L402)
  ML-DSA.Verify (FIPS 204, Algorithm 3) with the context string. Tagged `#NoDiscard`.
- **MlDsa.MlDsaVerifier** · [`export let verify_prehash = func(message: uint8[list], signature: uint8[list], context: uint8[list], hash: Algorithm) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L411)
  HashML-DSA.Verify (FIPS 204, Algorithm 5) with the pre-hash function `hash`. Tagged `#NoDiscard`.
- **MlDsa.MlDsaVerifier** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L419)
- **MlDsa.MlDsaVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-dsa.nra#L422)
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
