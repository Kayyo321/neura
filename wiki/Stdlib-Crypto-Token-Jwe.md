# `Std.Crypto.Token.Jwe`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Jwe` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Jwe** · [`export let ContentEncryption = enum (A128Gcm, A192Gcm, A256Gcm, A128CbcHs256, A192CbcHs384, A256CbcHs512)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L41)
  The content encryption algorithm bound to a JWE key. Since 0.3.0.
- **Jwe** · [`export let WrapAlgorithm = enum (A128Kw, A192Kw, A256Kw)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L46)
  The AES Key Wrap algorithm bound to a JWE wrapping key. Since 0.3.0.
- **Jwe** · [`export let EcdhAlgorithm = enum (Es, EsA128Kw, EsA192Kw, EsA256Kw)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L51)
  ECDH-ES direct key agreement and ECDH-ES with AES Key Wrap (RFC 7518, section 4.6). Since 0.3.0.
- **Jwe** · [`export let DirectKey = class implements SecretMemory<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L125)
  A direct JWE content encryption key. The only permitted key management algorithm is dir. Tagged `#Final`. Since 0.3.0.
- **Jwe.DirectKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L129)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.DirectKey** · [`export let init = func(enc: ContentEncryption, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L135)
  Copies the key. Its length must match the specified encryption algorithm. Since 0.3.0.
- **Jwe.DirectKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L147)
  A copy of the key. Exposing a wiped key traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.DirectKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L152)
  Overwrites and invalidates the stored key. Since 0.3.0.
- **Jwe.DirectKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L157)
  Whether the stored key has been invalidated. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.DirectKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L162)
  Requests the underlying SecretBytes storage lock. Since 0.3.0.
- **Jwe.DirectKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L167)
  A fixed placeholder for the key. Since 0.3.0.
- **Jwe** · [`export let WrappingKey = class implements SecretMemory<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L173)
  An AES JWE key-encryption key with both expected algorithms fixed by the caller. Tagged `#Final`. Since 0.3.0.
- **Jwe.WrappingKey** · [`export last algorithm: WrapAlgorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L177)
  The expected key wrap algorithm. Since 0.3.0.
- **Jwe.WrappingKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L181)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let init = func(alg: WrapAlgorithm, enc: ContentEncryption, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L187)
  Copies the wrapping key. Its length must match the specified wrap algorithm. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L201)
  A copy of the key. Exposing a wiped key traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L206)
  Overwrites and invalidates the stored key. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L211)
  Whether the stored key has been invalidated. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L216)
  Requests the underlying SecretBytes storage lock. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L221)
  A fixed placeholder for the key. Since 0.3.0.
- **Jwe** · [`export let GcmWrapAlgorithm = enum (A128GcmKw, A192GcmKw, A256GcmKw)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L227)
  The AES-GCM key encryption algorithm bound to a JWE wrapping key. Since 0.3.0.
- **Jwe** · [`export let Pbes2Algorithm = enum (Hs256A128Kw, Hs384A192Kw, Hs512A256Kw)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L232)
  The PBES2 HMAC and AES Key Wrap algorithms bound to a password. Since 0.3.0.
- **Jwe** · [`export let GcmWrappingKey = class implements SecretMemory<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L263)
  An AES-GCM wrapping key bound to the expected key encryption and content algorithms. A wrapping IV must never repeat under a KEK. RFC 7518 limits AES-GCM to 2^32 encryption operations per KEK across its users. The caller manages that limit across key objects and processes. Tagged `#Final`. Since 0.3.0.
- **Jwe.GcmWrappingKey** · [`export last algorithm: GcmWrapAlgorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L267)
  The expected key encryption algorithm. Since 0.3.0.
- **Jwe.GcmWrappingKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L272)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.GcmWrappingKey** · [`export let init = func(enc: ContentEncryption, alg: GcmWrapAlgorithm, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L278)
  Copies the wrapping key. Its byte count must match the key encryption algorithm. Since 0.3.0.
- **Jwe.GcmWrappingKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L296)
  Copies the key. Exposing a wiped key traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.GcmWrappingKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L301)
  Overwrites and invalidates the stored key. Since 0.3.0.
- **Jwe.GcmWrappingKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L306)
  Whether the stored key has been invalidated. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.GcmWrappingKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L311)
  Requests the underlying SecretBytes storage lock. Since 0.3.0.
- **Jwe.GcmWrappingKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L316)
  A fixed redacted display. Since 0.3.0.
- **Jwe** · [`export let Pbes2Key = class implements SecretMemory<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L322)
  A password bound to a PBES2 algorithm, a content algorithm, and a PBKDF2 iteration budget. Tagged `#Final`. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export last algorithm: Pbes2Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L326)
  The expected key management algorithm. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L331)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export last iteration_limit: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L336)
  The greatest PBKDF2 iteration count accepted when sealing or opening. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export let init = func(enc: ContentEncryption, alg: Pbes2Algorithm, password: uint8[list], max_iterations: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L343)
  Copies a password as octets. Encode a textual password in UTF-8 before importing it. Supply a positive maximum count independently of token data. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L365)
  Copies the password. Exposing a wiped password traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L370)
  Overwrites and invalidates the stored password. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L375)
  Whether the stored password has been invalidated. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L380)
  Requests the underlying SecretBytes storage lock. Since 0.3.0.
- **Jwe.Pbes2Key** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L385)
  A fixed redacted display. Since 0.3.0.
- **Jwe** · [`export let RsaWrappingKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L402)
  An RSA public key bound to RSA-OAEP-256 and one content encryption algorithm. Tagged `#Final`. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L406)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export last public_key: rsa.RsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L411)
  The RSA public key. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export let init = func(enc: ContentEncryption, rsa_key: rsa.RsaPublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L416)
  Binds the public key. A modulus shorter than 2048 bits traps. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L425)
  Whether the key and the content encryption algorithm are equal. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L435)
  A hash of the public key and the content algorithm. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L440)
  The key management and content encryption names. Since 0.3.0.
- **Jwe** · [`export let RsaUnwrappingKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L446)
  An RSA private key bound to RSA-OAEP-256 and one content encryption algorithm. Tagged `#Final`. Since 0.3.0.
- **Jwe.RsaUnwrappingKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L450)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.RsaUnwrappingKey** · [`export let init = func(enc: ContentEncryption, rsa_key: rsa.RsaPrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L456)
  Retains the private key. A modulus shorter than 2048 bits traps. Since 0.3.0.
- **Jwe.RsaUnwrappingKey** · [`export let open = func(compact: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L468)
  Authenticates and decrypts an RSA-OAEP-256 compact JWE. Traps after the underlying key is wiped. On failure to recover the expected CEK, it still checks content authentication using a pseudorandom substitute derived under the private key, and returns the same authentication error. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.RsaUnwrappingKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L501)
  A fixed placeholder for the key. Since 0.3.0.
- **Jwe** · [`export let EcdhEncryptionKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L545)
  A P-256, P-384, or P-521 recipient public key bound to ECDH-ES and the expected content algorithm. Tagged `#Final`. Since 0.3.0.
- **Jwe.EcdhEncryptionKey** · [`export last public_key: ecdsa.EcPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L549)
  The recipient's public key. Since 0.3.0.
- **Jwe.EcdhEncryptionKey** · [`export last algorithm: EcdhAlgorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L554)
  Whether the derived key encrypts content directly or first wraps a CEK. Since 0.3.0.
- **Jwe.EcdhEncryptionKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L559)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.EcdhEncryptionKey** · [`export let init = func(key: ecdsa.EcPublicKey, alg: EcdhAlgorithm, enc: ContentEncryption)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L564)
  Binds the recipient public key to one key-management and one content encryption algorithm. Since 0.3.0.
- **Jwe.EcdhEncryptionKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L574)
  Whether the recipient and both algorithms are equal. Since 0.3.0.
- **Jwe.EcdhEncryptionKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L584)
  A hash of the recipient and both algorithms. Since 0.3.0.
- **Jwe.EcdhEncryptionKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L590)
  The configured algorithm names. Since 0.3.0.
- **Jwe** · [`export let EcdhDecryptionKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L597)
  An ECDH-ES recipient private key with a bound key-management and content algorithm. Tagged `#Final`. Since 0.3.0.
- **Jwe.EcdhDecryptionKey** · [`export last algorithm: EcdhAlgorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L603)
  The expected key-management algorithm. Since 0.3.0.
- **Jwe.EcdhDecryptionKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L608)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.EcdhDecryptionKey** · [`export let init = func(recipient: ecdsa.EcPrivateKey, alg: EcdhAlgorithm, enc: ContentEncryption)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L613)
  Retains the recipient's private key. Wiping that key also disables this decryption key. Since 0.3.0.
- **Jwe.EcdhDecryptionKey** · [`export let open = func(compact: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L623)
  Authenticates and decrypts a compact JWE. Traps after the underlying private key is wiped. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.EcdhDecryptionKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L646)
  A fixed placeholder for the key. Since 0.3.0.
- **Jwe** · [`export let seal_with = func(key: DirectKey, plaintext: uint8[list], iv: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L1122)
  Deterministic compact encryption with a caller-supplied IV, and a CEK when using a wrapping key. The RSA-OAEP-256 overload also takes a 32-byte seed. Never reuse an IV under the same CEK. With a caller-supplied protected JSON header, its exact UTF-8 bytes are authenticated, and a malformed or mismatching header returns an error. ECDH-ES takes a fresh private ephemeral key on the recipient curve for each message. The direct mode derives its CEK and the wrapping modes take one. Party information is supplied as bytes, with an empty value omitted. For exact protected JSON, the header's epk must match the supplied ephemeral key. AES-GCM key wrapping takes a separate 12-byte wrapping IV and binds it and its tag in the header. An AES-GCM wrapping key must differ from the content encryption key. PBES2 takes fresh salt input of at least eight bytes and an iteration count of at least 1000 within the recipient's limit. An incorrect length or out-of-policy sealing count traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe** · [`export let seal = func(key: DirectKey, plaintext: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L1209)
  Compact encryption with fresh IV and, for a wrapping key, a fresh CEK. An OS entropy failure is returned to the caller. A direct AES-GCM key using random IVs is limited to 2^32 seal operations across all users of that key. Overloads accept a caller's DRBG. ECDH-ES generates a fresh ephemeral key each time and accepts optional producer and recipient information as separate byte strings. AES-GCM wrapping generates a separate fresh wrapping IV. PBES2 generates a 16-byte salt input and takes an explicit sealing count. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe** · [`export let open = func(key: DirectKey, compact: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L1252)
  Authenticates and decrypts a compact JWE using the caller's expected algorithms. Malformed input and an algorithm mismatch return crypto.invalid_jwe. Integrity and decryption failures return crypto.authentication_failed, with no unauthenticated plaintext. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
