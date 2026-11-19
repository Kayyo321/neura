# `Std.Crypto.Token.Biscuit`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Biscuit` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Biscuit** · [`export let KeyAlgorithm = enum (Ed25519, P256)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L18)
  Algorithm to use for a new Biscuit next key. Since 0.3.0.
- **Biscuit** · [`export let ExternalValue = externals.ExternalValue`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L23)
  An exact Datalog value delivered to an explicitly registered application function. Since 0.3.0.
- **Biscuit** · [`export let ExternalMapKey = externals.ExternalMapKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L28)
  A Datalog map key delivered to an explicitly registered application function. Since 0.3.0.
- **Biscuit** · [`export let ExternalFunction = externals.ExternalFunction`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L33)
  Binds a pure application callback to an unqualified Datalog external function name. Since 0.3.0.
- **Biscuit** · [`export let verify = func(token: uint8[list], root: ed25519.Ed25519PublicKey) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L55)
  Checks the signed chain and its open or sealed proof using an explicitly trusted root. The caller uses authorize for access decisions. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let authorize = func(token: uint8[list], root: ed25519.Ed25519PublicKey, code: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L77)
  Grants access only when the verified token's checks and the first matching policy allow it. Default Datalog scopes isolate holder-signed facts from the authority and authorizer. Explicitly supply any application functions for this call. Only register pure callbacks that can handle arbitrary token-selected operands. Unregistered names cause an authorization error. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let mint_with = func(code: string, root: ed25519.Ed25519PrivateKey, next_seed: uint8[list], algorithm: KeyAlgorithm = KeyAlgorithm.Ed25519) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L127)
  Mints an open token using fresh, caller-supplied 32-byte next-key material. Ed25519 uses a seed, and P-256 uses a canonical big-endian scalar. A wrong size traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let mint = func(code: string, root: ed25519.Ed25519PrivateKey, algorithm: KeyAlgorithm = KeyAlgorithm.Ed25519) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L137)
  Mints an open token with a fresh next key from OS entropy. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let append_with = func(token: uint8[list], root: ed25519.Ed25519PublicKey, code: string, next_seed: uint8[list], algorithm: KeyAlgorithm = KeyAlgorithm.Ed25519) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L163)
  Appends one or more checks to a verified open token using a fresh caller-supplied next key. A wrong next-key size traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let append = func(token: uint8[list], root: ed25519.Ed25519PublicKey, code: string, algorithm: KeyAlgorithm = KeyAlgorithm.Ed25519) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L173)
  Appends checks to a verified open token with a fresh next key from OS entropy. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let seal = func(token: uint8[list], root: ed25519.Ed25519PublicKey) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L187)
  Seals a verified open token by replacing its next-key secret with a final signature. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let revocation_identifiers = func(token: uint8[list], root: ed25519.Ed25519PublicKey) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L201)
  Returns each verified block's wire signature in chain order, starting with the authority. Use stable_revocation_identifiers when comparing P-256 credentials against a revocation list. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let stable_revocation_identifiers = func(token: uint8[list], root: ed25519.Ed25519PublicKey) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L210)
  Returns each verified block's signature for an application revocation list. P-256 signatures use the low-S DER representative. Normalize stored identifiers with the same API. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let third_party_request = func(token: uint8[list], root: ed25519.Ed25519PublicKey) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L223)
  Encodes a request bound to the last signature of a verified open Biscuit. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let create_third_party = func(request: uint8[list], code: string, signer: ed25519.Ed25519PrivateKey) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L237)
  Signs a third-party block response. The third party supplies the facts and checks in code. Treat the request as opaque input and authenticate the requester independently if needed. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let append_third_party_with = func(token: uint8[list], root: ed25519.Ed25519PublicKey, expected_signer: string, response: uint8[list], next_seed: uint8[list], algorithm: KeyAlgorithm = KeyAlgorithm.Ed25519) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L262)
  Appends a verified third-party response using fresh caller-supplied next-key material. The expected signer is the Datalog public-key literal. A wrong next-key size traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Biscuit** · [`export let append_third_party = func(token: uint8[list], root: ed25519.Ed25519PublicKey, expected_signer: string, response: uint8[list], algorithm: KeyAlgorithm = KeyAlgorithm.Ed25519) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/biscuit.nra#L274)
  Appends a verified third-party response with a fresh next key from OS entropy. The expected signer is the Datalog public-key literal. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
