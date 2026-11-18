# `Std.Crypto.Token.Paseto`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Paseto` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Paseto** · [`export let OpenedToken = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L32)
  The authenticated payload and footer of an opened token. Accessors return copies. Tagged `#Final`. Since 0.3.0.
- **Paseto.OpenedToken** · [`export let init = func(payload: uint8[list], footer: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L39)
  Copies the payload and footer bytes. Since 0.3.0.
- **Paseto.OpenedToken** · [`export let payload = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L47)
  A copy of the payload. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.OpenedToken** · [`export let footer = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L52)
  A copy of the authenticated footer, or an empty list when absent. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.OpenedToken** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L57)
  Whether both payloads and both footers are equal byte for byte. Since 0.3.0.
- **Paseto.OpenedToken** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L69)
  A hash of the payload and footer. Since 0.3.0.
- **Paseto.OpenedToken** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L83)
  A fixed placeholder for the token contents. Since 0.3.0.
- **Paseto** · [`export let Local = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L89)
  PASETO v4.local under a purpose-bound 32-byte key. Tagged `#Final`. Since 0.3.0.
- **Paseto.Local** · [`export let init = func(local_key: SymmetricKey<Local>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L95)
  Retains the key. A key of another length traps with invalid argument. Since 0.3.0.
- **Paseto.Local** · [`export let encrypt = func(plaintext: uint8[list], footer: uint8[list], implicit_assertion: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L107)
  Encrypts with a fresh 32-byte OS nonce. Entropy failures are returned. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.Local** · [`export let encrypt_with_nonce = func(plaintext: uint8[list], nonce: uint8[list], footer: uint8[list], implicit_assertion: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L117)
  Encrypts with the given 32-byte nonce; another length traps. Use a fresh random nonce for every encryption with the same key. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.Local** · [`export let decrypt = func(token: string, implicit_assertion: uint8[list]) -> Result<OpenedToken>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L134)
  Authenticates before decrypting. Returns crypto.token for malformed framing or encoding and crypto.authentication for a failed tag check. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.Local** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L163)
  A fixed placeholder for the local key. Since 0.3.0.
- **Paseto** · [`export let PublicSigner = class implements Signer<ed25519.Ed25519PrivateKey, string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L169)
  A signer producing PASETO v4.public tokens. Use a key dedicated to this purpose. Tagged `#Final`. Since 0.3.0.
- **Paseto.PublicSigner** · [`export let init = func(key: ed25519.Ed25519PrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L175)
  Retains the Ed25519 private key for issuing v4.public tokens. Since 0.3.0.
- **Paseto.PublicSigner** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L182)
  The underlying algorithm, Ed25519. Since 0.3.0.
- **Paseto.PublicSigner** · [`export let sign = func(message: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L187)
  Signs the payload with no footer or implicit assertion. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.PublicSigner** · [`export let sign_with = func(message: uint8[list], footer: uint8[list], implicit_assertion: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L192)
  Signs the payload and binds the footer and implicit assertion. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.PublicSigner** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L202)
  A fixed placeholder for the signing key. Since 0.3.0.
- **Paseto** · [`export let PublicVerifier = class implements Verifier<ed25519.Ed25519PublicKey, string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L208)
  A verifier accepting only PASETO v4.public tokens. Tagged `#Final`. Since 0.3.0.
- **Paseto.PublicVerifier** · [`export let init = func(key: ed25519.Ed25519PublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L214)
  Retains the Ed25519 verification key. Since 0.3.0.
- **Paseto.PublicVerifier** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L221)
  The underlying algorithm, Ed25519. Since 0.3.0.
- **Paseto.PublicVerifier** · [`export let public_key = func -> ed25519.Ed25519PublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L226)
  The public verification key. Since 0.3.0.
- **Paseto.PublicVerifier** · [`export let verify = func(message: uint8[list], signature: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L231)
  Whether the token authenticates the expected payload with an empty implicit assertion. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.PublicVerifier** · [`export let open = func(token: string, implicit_assertion: uint8[list]) -> Result<OpenedToken>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L240)
  Verifies the signature and returns the authenticated payload and footer. Returns crypto.token for malformed framing or encoding and crypto.authentication for a bad signature. Tagged `#NoDiscard`. Since 0.3.0.
- **Paseto.PublicVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/paseto.nra#L258)
  The public verifier's type. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
