# `Std.Crypto.Pki.PrivateKeyInfo`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.PrivateKeyInfo` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PrivateKeyInfo** · [`export let PrivateKeyInfo = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L33)
  A OneAsymmetricKey: version 0 (v1) or 1 (v2, which may carry the public key). Tagged `#Final`. Since 0.3.0.
- **PrivateKeyInfo.PrivateKeyInfo** · [`export last version: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L35)
  0 for v1 (PrivateKeyInfo), 1 for v2.
- **PrivateKeyInfo.PrivateKeyInfo** · [`export last algorithm: AlgorithmIdentifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L36)
- **PrivateKeyInfo.PrivateKeyInfo** · [`export last private_key: secret_bytes.SecretBytes`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L38)
  The contents of the privateKey OCTET STRING (for Ed25519, itself an encoded OCTET STRING).
- **PrivateKeyInfo.PrivateKeyInfo** · [`export last attributes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L40)
  The DER of the [0] attributes, or empty.
- **PrivateKeyInfo.PrivateKeyInfo** · [`export last public_key: Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L42)
  The [1] publicKey bytes (v2 only).
- **PrivateKeyInfo.PrivateKeyInfo** · [`export let init = func(version_number: int, id: AlgorithmIdentifier, key: secret_bytes.SecretBytes, attribute_der: uint8[list], public_bytes: Optional<uint8[list]>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L44)
- **PrivateKeyInfo.PrivateKeyInfo** · [`export let encode = func -> secret_bytes.SecretBytes`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L54)
  The DER encoding, held as a secret.
- **PrivateKeyInfo.PrivateKeyInfo** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L74)
  Overwrites the private key bytes.
- **PrivateKeyInfo.PrivateKeyInfo** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L77)
  A fixed placeholder for the private key.
- **PrivateKeyInfo** · [`export let EncryptedPrivateKeyInfo = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L82)
  An EncryptedPrivateKeyInfo with PBES2 parameters read out. Not secret: compared by DER. Tagged `#Final`. Since 0.3.0.
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L83)
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last salt: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L84)
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last iterations: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L85)
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last key_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L87)
  The PBKDF2 keyLength, or -1 when absent.
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last prf: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L89)
  The PRF OID (hmacWithSHA1 when absent, the PBKDF2 default).
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last cipher: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L91)
  The encryption scheme OID (AES-CBC or AES-GCM).
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last iv: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L93)
  The CBC IV or the GCM nonce.
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last tag_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L95)
  The GCM tag length (the ICVlen), 0 for CBC.
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export last encrypted_data: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L96)
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export let init = func(encoded: uint8[list], salt_bytes: uint8[list], count: int, length: int, prf_id: Oid, cipher_id: Oid, iv_bytes: uint8[list], icv: int, data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L98)
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export let decrypt = func(password: uint8[list]) -> Result<PrivateKeyInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L113)
  Decrypts with the password (its UTF-8 bytes, as RFC 8018 recommends). Tagged `#NoDiscard`.
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L150)
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L166)
- **PrivateKeyInfo.EncryptedPrivateKeyInfo** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L169)
  "EncryptedPrivateKeyInfo(<cipher>, <iterations> iterations)".
- **PrivateKeyInfo** · [`export let parse = func(bytes: uint8[list]) -> Result<PrivateKeyInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L212)
  Parses a OneAsymmetricKey. A v1 key with a public key, or a version other than 0 or 1, fails. Tagged `#NoDiscard`. Since 0.3.0.
- **PrivateKeyInfo** · [`export let parse_pem = func(text: string) -> Result<PrivateKeyInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L252)
  The only PRIVATE KEY block of PEM text, parsed. Tagged `#NoDiscard`. Since 0.3.0.
- **PrivateKeyInfo** · [`export let parse_encrypted = func(bytes: uint8[list]) -> Result<EncryptedPrivateKeyInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L257)
  Parses an EncryptedPrivateKeyInfo. Only PBES2 is accepted (`crypto.unsupported` otherwise), with PBKDF2 and an AES-CBC or AES-GCM scheme; malformed parameters are a `Parse` error. Tagged `#NoDiscard`. Since 0.3.0.
- **PrivateKeyInfo** · [`export let encrypt = func(info: PrivateKeyInfo, password: uint8[list], salt: uint8[list], iterations: int, prf: string, cipher: string, iv: uint8[list]) -> EncryptedPrivateKeyInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L346)
  Encrypts with PBES2: PBKDF2 under the PRF (hmacWithSHA224/256/384/512, dotted) with the salt and iteration count, then the cipher (AES-128/192/256-CBC with a 16-byte IV, or AES-128/192/256-GCM with a 12-byte nonce in `iv`). The keyLength field is omitted. A bad argument traps with "invalid argument: ...". Tagged `#NoDiscard`. Since 0.3.0.
- **PrivateKeyInfo** · [`export let encrypt_random = func(info: PrivateKeyInfo, password: uint8[list], iterations: int) -> Result<EncryptedPrivateKeyInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/private-key-info.nra#L388)
  `encrypt` with a 16-byte salt and the IV or nonce drawn from the operating system's generator, PBKDF2-HMAC-SHA-256 and AES-256-GCM. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
