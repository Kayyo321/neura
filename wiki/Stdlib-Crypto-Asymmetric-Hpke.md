# `Std.Crypto.Asymmetric.Hpke`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.Hpke` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Hpke** · [`export let Kem = enum ( DhKemP256HkdfSha256, DhKemP384HkdfSha384, DhKemP521HkdfSha512, DhKemX25519HkdfSha256, DhKemX448HkdfSha512 )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L40)
  The KEMs of RFC 9180, section 7.1. Since 0.3.0.
- **Hpke** · [`export let Kdf = enum (HkdfSha256, HkdfSha384, HkdfSha512)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L47)
  The KDFs of RFC 9180, section 7.2. Since 0.3.0.
- **Hpke** · [`export let Aead = enum (Aes128Gcm, Aes256Gcm, ChaCha20Poly1305, ExportOnly)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L50)
  The AEADs of RFC 9180, section 7.3. Since 0.3.0.
- **Hpke** · [`export let Mode = enum (Base, Psk, Auth, AuthPsk)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L53)
  The modes of RFC 9180, section 5. Since 0.3.0.
- **Hpke** · [`export let kem_id = func(kem: Kem) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L57)
  The KEM's identifier: 0x0010, 0x0011, 0x0012, 0x0020 or 0x0021. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let kdf_id = func(kdf: Kdf) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L67)
  The KDF's identifier: 1, 2 or 3. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let aead_id = func(aead: Aead) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L75)
  The AEAD's identifier: 1, 2, 3 or 0xFFFF. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let public_key_length = func(kem: Kem) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L110)
  Npk (and Nenc). Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let private_key_length = func(kem: Kem) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L120)
  Nsk. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let HpkePublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L196)
  An HPKE public key: SerializePublicKey of RFC 9180, section 7.1.1 (an uncompressed SEC1 point, or the X25519 or X448 bytes). Equal keys have equal KEMs and bytes. Tagged `#Final`. Since 0.3.0.
- **Hpke.HpkePublicKey** · [`export last kem: Kem`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L198)
  The KEM.
- **Hpke.HpkePublicKey** · [`export let init = func(key_kem: Kem, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L203)
  Checks and copies the encoding; a wrong length or a P-curve encoding that is not an uncompressed point of the curve traps (use `parse_public_key` for outside input).
- **Hpke.HpkePublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L215)
  A copy of the encoding. Tagged `#NoDiscard`.
- **Hpke.HpkePublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L217)
- **Hpke.HpkePublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L224)
- **Hpke.HpkePublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L232)
- **Hpke** · [`export let HpkePrivateKey = class implements PrivateKey<DhKem>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L254)
  An HPKE private key (Nsk bytes in secret memory) with its public key. `to_string` never prints it. Tagged `#Final`. Since 0.3.0.
- **Hpke.HpkePrivateKey** · [`export last kem: Kem`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L256)
  The KEM.
- **Hpke.HpkePrivateKey** · [`export let init = func(key_kem: Kem, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L262)
  The key of its Nsk bytes (for a P-curve, a scalar from 1 to n - 1). Another length or scalar traps (use `parse_private_key` for outside input).
- **Hpke.HpkePrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L274)
- **Hpke.HpkePrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L275)
- **Hpke.HpkePrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L276)
- **Hpke.HpkePrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L277)
- **Hpke.HpkePrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L278)
- **Hpke.HpkePrivateKey** · [`export let public_key = func -> HpkePublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L282)
  The matching public key. Tagged `#NoDiscard`.
- **Hpke.HpkePrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L285)
  A fixed placeholder, never the key.
- **Hpke** · [`export let derive_key_pair = func(kem: Kem, ikm: uint8[list]) -> HpkePrivateKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L341)
  DeriveKeyPair of RFC 9180, section 7.1.3, from input keying material (at least Nsk bytes are recommended). A P-curve derivation that finds no scalar in 256 candidates, which has negligible probability, traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let generate_key_pair = func(kem: Kem) -> Result<HpkePrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L369)
  A new key pair from Nsk bytes of `Entropy.os_bytes`, or of the given generator, through DeriveKeyPair. Fails when the entropy source fails. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let parse_public_key = func(kem: Kem, bytes: uint8[list]) -> Result<HpkePublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L390)
  A public key from outside the program. A wrong length or a P-curve encoding that is not an uncompressed point of the curve is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let parse_private_key = func(kem: Kem, bytes: uint8[list]) -> Result<HpkePrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L400)
  A private key from outside the program. A wrong length or a P-curve scalar outside 1 to n - 1 is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke** · [`export let DhKem = class implements KeyEncapsulation<HpkePublicKey, HpkePrivateKey, SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L410)
  A DHKEM of RFC 9180, section 4.1, as a `KeyEncapsulation`: the ciphertext is `enc`, the serialized ephemeral public key, and the shared secret has Nsecret bytes. Tagged `#Final`. Since 0.3.0.
- **Hpke.DhKem** · [`export last kem: Kem`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L412)
  The KEM.
- **Hpke.DhKem** · [`export let init = func(key_kem: Kem)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L417)
  The KEM with ephemeral keys from `Entropy.os_bytes`, or from the given generator.
- **Hpke.DhKem** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L433)
  `Algorithm.Hpke`.
- **Hpke.DhKem** · [`export let ephemeral_key = func -> Result<HpkePrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L437)
  A fresh ephemeral key pair. Tagged `#NoDiscard`.
- **Hpke.DhKem** · [`export let encapsulate_with = func(public_key: HpkePublicKey, ephemeral: HpkePrivateKey) -> Result<Encapsulation<SecretBytes, uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L447)
  Encap(pkR) with the given ephemeral key. An X25519 or X448 key that gives an all-zero secret is `Custom` code `crypto.key_agreement`. Keys of another KEM trap. Tagged `#NoDiscard`.
- **Hpke.DhKem** · [`export let auth_encapsulate_with = func(public_key: HpkePublicKey, sender: HpkePrivateKey, ephemeral: HpkePrivateKey) -> Result<Encapsulation<SecretBytes, uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L461)
  AuthEncap(pkR, skS) with the given ephemeral key. Tagged `#NoDiscard`.
- **Hpke.DhKem** · [`export let try_encapsulate = func(public_key: HpkePublicKey) -> Result<Encapsulation<SecretBytes, uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L478)
  Encap(pkR) with a fresh ephemeral key. Fails on an entropy failure or a bad X25519/X448 key. Tagged `#NoDiscard`.
- **Hpke.DhKem** · [`export let try_auth_encapsulate = func(public_key: HpkePublicKey, sender: HpkePrivateKey) -> Result<Encapsulation<SecretBytes, uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L491)
  AuthEncap(pkR, skS) with a fresh ephemeral key. Tagged `#NoDiscard`.
- **Hpke.DhKem** · [`export let encapsulate = func(public_key: HpkePublicKey) -> Encapsulation<SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L505)
  Encap(pkR) with a fresh ephemeral key. Traps with `unavailable: ...` when `try_encapsulate` fails (an entropy failure, or an X25519/X448 key that gives an all-zero secret).
- **Hpke.DhKem** · [`export let decapsulate = func(private_key: HpkePrivateKey, ciphertext: uint8[list]) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L518)
  Decap(enc, skR). A malformed `enc` or a failed Diffie-Hellman is `Custom` code `crypto.invalid_ciphertext`.
- **Hpke.DhKem** · [`export let auth_decapsulate = func(private_key: HpkePrivateKey, ciphertext: uint8[list], sender: HpkePublicKey) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L532)
  AuthDecap(enc, skR, pkS). Tagged `#NoDiscard`.
- **Hpke.DhKem** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L562)
- **Hpke** · [`export let SenderContext = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L624)
  The sender's context: `enc` for the recipient, `seal` in order, and `export_secret`. Tagged `#Final`. Since 0.3.0.
- **Hpke.SenderContext** · [`export last enc: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L626)
  The encapsulated key to send with the messages.
- **Hpke.SenderContext** · [`export let sequence: int64 = 0 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L629)
  The number of messages sealed so far.
- **Hpke.SenderContext** · [`export let init = func(encapsulated: uint8[list], keys: Schedule)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L632)
- **Hpke.SenderContext** · [`export let seal = func(aad: uint8[list], plaintext: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L639)
  The ciphertext of the next message. An export-only context traps. Tagged `#NoDiscard`.
- **Hpke.SenderContext** · [`export let export_secret = func(exporter_context: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L647)
  `length` bytes of exported secret for the context string (at most 255 Nh bytes). Tagged `#NoDiscard`.
- **Hpke.SenderContext** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L650)
- **Hpke** · [`export let ReceiverContext = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L655)
  The recipient's context: `open` in order, and `export_secret`. Tagged `#Final`. Since 0.3.0.
- **Hpke.ReceiverContext** · [`export let sequence: int64 = 0 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L658)
  The number of messages opened so far.
- **Hpke.ReceiverContext** · [`export let init = func(keys: Schedule)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L661)
- **Hpke.ReceiverContext** · [`export let open = func(aad: uint8[list], ciphertext: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L668)
  The plaintext of the next message, or `Custom` code `crypto.authentication`; a failure does not advance the sequence number. An export-only context traps. Tagged `#NoDiscard`.
- **Hpke.ReceiverContext** · [`export let export_secret = func(exporter_context: uint8[list], length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L682)
  `length` bytes of exported secret for the context string (at most 255 Nh bytes). Tagged `#NoDiscard`.
- **Hpke.ReceiverContext** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L685)
- **Hpke** · [`export let SealedMessage = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L690)
  The output of the single-shot `seal`: `enc` and the ciphertext. Tagged `#Final`. Since 0.3.0.
- **Hpke.SealedMessage** · [`export last enc: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L691)
- **Hpke.SealedMessage** · [`export last ciphertext: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L692)
- **Hpke.SealedMessage** · [`export let init = func(encapsulated: uint8[list], sealed: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L694)
- **Hpke.SealedMessage** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L702)
  Whether both the encapsulation and ciphertext bytes match. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke.SealedMessage** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L712)
  A hash of both byte sequences, including their lengths. Tagged `#NoDiscard`. Since 0.3.0.
- **Hpke.SealedMessage** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L723)
- **Hpke** · [`export let Suite = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L729)
  An HPKE ciphersuite (KEM, KDF, AEAD). Tagged `#Final`. Since 0.3.0.
- **Hpke.Suite** · [`export last kem: Kem`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L730)
- **Hpke.Suite** · [`export last kdf: Kdf`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L731)
- **Hpke.Suite** · [`export last aead: Aead`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L732)
- **Hpke.Suite** · [`export let init = func(suite_kem: Kem, suite_kdf: Kdf, suite_aead: Aead)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L736)
  The suite, with ephemeral keys from `Entropy.os_bytes` or from the given generator.
- **Hpke.Suite** · [`export let key_encapsulation = func -> DhKem`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L768)
  The DHKEM of the suite. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_sender_with = func(mode: Mode, public_key: HpkePublicKey, info: uint8[list], psk: uint8[list], psk_id: uint8[list], sender: HpkePrivateKey[list], ephemeral: HpkePrivateKey) -> Result<SenderContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L806)
  SetupS of any mode with the given ephemeral key: `psk` and `psk_id` are empty in the base and auth modes, and `sender` holds the sender's key in the auth modes (and is empty otherwise). Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_sender = func(mode: Mode, public_key: HpkePublicKey, info: uint8[list], psk: uint8[list], psk_id: uint8[list], sender: HpkePrivateKey[list]) -> Result<SenderContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L827)
  SetupS of any mode with a fresh ephemeral key (see `setup_sender_with`). Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_base_sender = func(public_key: HpkePublicKey, info: uint8[list]) -> Result<SenderContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L841)
  SetupBaseS. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_psk_sender = func(public_key: HpkePublicKey, info: uint8[list], psk: uint8[list], psk_id: uint8[list]) -> Result<SenderContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L846)
  SetupPSKS. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_auth_sender = func(public_key: HpkePublicKey, info: uint8[list], sender: HpkePrivateKey) -> Result<SenderContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L851)
  SetupAuthS. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_auth_psk_sender = func(public_key: HpkePublicKey, info: uint8[list], psk: uint8[list], psk_id: uint8[list], sender: HpkePrivateKey) -> Result<SenderContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L856)
  SetupAuthPSKS. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_receiver = func(mode: Mode, enc: uint8[list], private_key: HpkePrivateKey, info: uint8[list], psk: uint8[list], psk_id: uint8[list], sender: HpkePublicKey[list]) -> Result<ReceiverContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L863)
  SetupR of any mode: `sender` holds the sender's public key in the auth modes (and is empty otherwise). A malformed `enc` or a failed Diffie-Hellman is `Custom` code `crypto.invalid_ciphertext`. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_base_receiver = func(enc: uint8[list], private_key: HpkePrivateKey, info: uint8[list]) -> Result<ReceiverContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L882)
  SetupBaseR. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_psk_receiver = func(enc: uint8[list], private_key: HpkePrivateKey, info: uint8[list], psk: uint8[list], psk_id: uint8[list]) -> Result<ReceiverContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L887)
  SetupPSKR. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_auth_receiver = func(enc: uint8[list], private_key: HpkePrivateKey, info: uint8[list], sender: HpkePublicKey) -> Result<ReceiverContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L893)
  SetupAuthR. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let setup_auth_psk_receiver = func(enc: uint8[list], private_key: HpkePrivateKey, info: uint8[list], psk: uint8[list], psk_id: uint8[list], sender: HpkePublicKey) -> Result<ReceiverContext>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L899)
  SetupAuthPSKR. Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let seal = func(public_key: HpkePublicKey, info: uint8[list], aad: uint8[list], plaintext: uint8[list]) -> Result<SealedMessage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L905)
  Single-shot base-mode encryption (RFC 9180, section 6.1). Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let open = func(enc: uint8[list], private_key: HpkePrivateKey, info: uint8[list], aad: uint8[list], ciphertext: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L919)
  Single-shot base-mode decryption (RFC 9180, section 6.1). Tagged `#NoDiscard`.
- **Hpke.Suite** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/hpke.nra#L931)
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
