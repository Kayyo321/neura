# `Std.Crypto.Pqc.MlKem`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pqc.MlKem` to use this module. The [package specification](Stdlib-Design#219-stdcryptopqc) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MlKem** · [`export let ParameterSet = enum (MlKem512, MlKem768, MlKem1024)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L31)
  The parameter sets of FIPS 203, section 8: ML-KEM-512, ML-KEM-768 and ML-KEM-1024. Since 0.3.0.
- **MlKem** · [`export let name = func(set: ParameterSet) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L49)
  The standard's name of the parameter set, such as "ML-KEM-768". Tagged `#NoDiscard`. Since 0.3.0.
- **MlKem** · [`export let public_key_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L57)
  The encapsulation-key length in bytes: 800, 1184 or 1568. Tagged `#NoDiscard`. Since 0.3.0.
- **MlKem** · [`export let private_key_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L61)
  The decapsulation-key length in bytes: 1632, 2400 or 3168. Tagged `#NoDiscard`. Since 0.3.0.
- **MlKem** · [`export let ciphertext_length = func(set: ParameterSet) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L65)
  The ciphertext length in bytes: 768, 1088 or 1568. Tagged `#NoDiscard`. Since 0.3.0.
- **MlKem** · [`export const SharedSecretLength = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L73)
  The length of the shared secret, 32 bytes for every parameter set. Since 0.3.0.
- **MlKem** · [`export let MlKemPublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L80)
  An ML-KEM encapsulation key. Every instance has passed the length and modulus checks of FIPS 203, section 7.2. Equal keys have equal bytes. Tagged `#Final`. Since 0.3.0.
- **MlKem.MlKemPublicKey** · [`export last parameter_set: ParameterSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L82)
  The parameter set.
- **MlKem.MlKemPublicKey** · [`export let init = func(set: ParameterSet, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L88)
  Copies the encoded key. A wrong length, or a key that fails the modulus check, traps with `invalid argument`; use `MlKem.parse_public_key` for keys from outside the program.
- **MlKem.MlKemPublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L103)
  A copy of the encoded key. Tagged `#NoDiscard`.
- **MlKem.MlKemPublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L105)
- **MlKem.MlKemPublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L122)
- **MlKem.MlKemPublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L130)
- **MlKem** · [`export let MlKemPrivateKey = class implements PrivateKey<MlKem>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L137)
  An ML-KEM decapsulation key (dk_PKE || ek || H(ek) || z) in secret memory. Every instance has passed the length and hash checks of FIPS 203, section 7.3. `to_string` never prints it. Tagged `#Final`. Since 0.3.0.
- **MlKem.MlKemPrivateKey** · [`export last parameter_set: ParameterSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L139)
  The parameter set.
- **MlKem.MlKemPrivateKey** · [`export let init = func(set: ParameterSet, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L144)
  Copies the encoded key. A wrong length, or a key whose stored H(ek) does not match, traps with `invalid argument`; use `MlKem.parse_private_key` for keys from outside the program.
- **MlKem.MlKemPrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L157)
- **MlKem.MlKemPrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L158)
- **MlKem.MlKemPrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L159)
- **MlKem.MlKemPrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L160)
- **MlKem.MlKemPrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L163)
  The encapsulation key stored inside the decapsulation key.
- **MlKem.MlKemPrivateKey** · [`export let public_key = func -> MlKemPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L170)
  The matching public key. Tagged `#NoDiscard`.
- **MlKem.MlKemPrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L173)
  A fixed placeholder, never the key.
- **MlKem** · [`export let MlKem = class implements KeyEncapsulation<MlKemPublicKey, MlKemPrivateKey, SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L179)
  An ML-KEM parameter set as a `KeyEncapsulation`. The shared secret is a 32-byte `SecretBytes` and the ciphertext a byte list. Tagged `#Final`. Since 0.3.0.
- **MlKem.MlKem** · [`export last parameter_set: ParameterSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L181)
  The parameter set.
- **MlKem.MlKem** · [`export let init = func(set: ParameterSet)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L187)
  The parameter set, with randomness from `Entropy.os_bytes`, or from the given generator.
- **MlKem.MlKem** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L200)
  `MlKem512`, `MlKem768` or `MlKem1024`.
- **MlKem.MlKem** · [`export let key_pair_from_seed = func(d: uint8[list], z: uint8[list]) -> MlKemPrivateKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L212)
  ML-KEM.KeyGen_internal (FIPS 203, Algorithm 16) from the 32-byte seeds d and z. Another length traps. Tagged `#NoDiscard`.
- **MlKem.MlKem** · [`export let generate_key_pair = func -> Result<MlKemPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L225)
  ML-KEM.KeyGen (FIPS 203, Algorithm 19) with 64 random bytes. Fails when the entropy source fails. Tagged `#NoDiscard`.
- **MlKem.MlKem** · [`export let encapsulate_with = func(public_key: MlKemPublicKey, m: uint8[list]) -> Encapsulation<SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L247)
  ML-KEM.Encaps_internal (FIPS 203, Algorithm 17) with the given 32-byte m. Another length, or a key of another parameter set, traps. Tagged `#NoDiscard`.
- **MlKem.MlKem** · [`export let try_encapsulate = func(public_key: MlKemPublicKey) -> Result<Encapsulation<SecretBytes, uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L262)
  ML-KEM.Encaps (FIPS 203, Algorithm 20) with 32 random bytes. Fails when the entropy source fails. Tagged `#NoDiscard`.
- **MlKem.MlKem** · [`export let encapsulate = func(public_key: MlKemPublicKey) -> Encapsulation<SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L277)
  ML-KEM.Encaps with 32 random bytes. Traps with `unavailable: ...` when the entropy source fails; `try_encapsulate` returns that failure instead.
- **MlKem.MlKem** · [`export let decapsulate = func(private_key: MlKemPrivateKey, ciphertext: uint8[list]) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L291)
  ML-KEM.Decaps (FIPS 203, Algorithm 21). A ciphertext of the wrong length fails with `Custom` code `crypto.invalid_ciphertext`; any ciphertext of the right length gives a key, the implicit rejection key when it was altered. A key of another parameter set traps.
- **MlKem.MlKem** · [`export let parse_public_key = func(bytes: uint8[list]) -> Result<MlKemPublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L306)
  An encapsulation key from outside the program, after the length and modulus checks of FIPS 203, section 7.2. A failure is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`.
- **MlKem.MlKem** · [`export let parse_private_key = func(bytes: uint8[list]) -> Result<MlKemPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L320)
  A decapsulation key from outside the program, after the length and hash checks of FIPS 203, section 7.3. A failure is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`.
- **MlKem.MlKem** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ml-kem.nra#L331)
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
