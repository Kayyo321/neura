# Neura 0.3.0: `Std.Crypto`, part 1

This note records the shape of the first part of `Std.Crypto`: the base contracts of the specification's contract tree, `Core`, `Random`, `Hash`, `Mac`, `Kdf` and `Symmetric` (sections 2.0 to 2.5 and 2.8 of the owner's library specification). The library contract is [Stdlib, section 20](../lang/Stdlib.md#20-stdcrypto). The public-key, post-quantum, PKI, token, enclave and advanced packages (2.6, 2.7 and 2.9 to 2.12) build on these contracts in a later change.

Status: *proposed* on 2026-11-17, implemented by autonomous agents on the `std-crypto` branch, awaiting owner confirmation. The rows below say which parts are implemented. Decisions that the owner has not confirmed are listed at the end.

## Purpose

`Std.Crypto` gives Neura programs hashes, MACs, key derivation, authenticated encryption and a deterministic random generator. The code is Neura source on the interpreter. It runs no C kernel except the existing SHA-256 kernel behind `Std.Digest.Sha256`. Each primitive is checked byte for byte against known answers from an independent implementation or a standard's text. The library makes no claim the runtime cannot keep. It says plainly that interpreted code does not run in constant time, that memory is not locked, and that copies of a secret made by the collector are not erased.

## Contract tree

Neura has interfaces with type parameters but no value parameters, and a class cannot implement two instances of one generic interface (`E0503`). The tree maps as follows.

| Specification | Neura | Status |
| --- | --- | --- |
| `SecretMemory[T]` | `Std.Crypto.SecretMemory.SecretMemory<T>`: `expose`, `wipe`, `is_wiped`, `lock` | implemented |
| `SymmetricKey[Alg]` | `SymmetricKey<Alg>: SecretMemory<uint8[list]>`, with `key_length`. `Alg` is the class of the primitive that takes the key | implemented |
| `PrivateKey[Alg]` | `PrivateKey<Alg>: SecretMemory<uint8[list]>`, with `public_key_bytes` | implemented (contract only) |
| `CryptographicPrimitive[Alg]` | `CryptographicPrimitive<Alg>`, with `algorithm() -> Algorithm` for the policy | implemented |
| `StreamingDigest[OutLen]` | `StreamingDigest<D>: CryptographicPrimitive<D>`. `D` is the implementing class. `OutLen` becomes the static requirement `output_length()`, and there are also `create()` and `block_length()`, so generic code calls `H.create()` | implemented |
| (XOF of 2.3) | `ExtendableOutput<D>: StreamingDigest<D>`, with `squeeze(count)`, plus `read(max)` and `reader()` in the reader shape of `Std.Io` | implemented |
| `AeadCipher[Key, Nonce, Tag]` | `AeadCipher<Key, Nonce, Tag>: CryptographicPrimitive<Key>`, with `seal`, `open`, `nonce_length` and `tag_length`. Every cipher here uses byte lists for `Nonce` and `Tag` | implemented |
| `Signer[PrivKey, Sig]` and `Verifier[PubKey, Sig]` | `Signer<PrivKey, Sig>` and `Verifier<PubKey, Sig>`, both `CryptographicPrimitive<Sig>`, so one class can implement both | implemented (contract only) |
| `KeyEncapsulation[PubKey, PrivKey, SharedSecret, Ciphertext]` | The same parameters, `CryptographicPrimitive<Ciphertext>`, with `encapsulate` returning `Encapsulation<SharedSecret, Ciphertext>` and `decapsulate` | implemented (contract only) |
| `KeyStoreProvider[KeyHandle]` | `KeyStoreProvider<KeyHandle>`, with `provider_name`, `generate_key`, `sign`, `wrap_key`, `unwrap_key` and `delete_key` | implemented (contract only) |

`tests/stdlib/crypto-contracts` implements every contract with a toy class and calls it through the interface.

## Tiers

"KAT" means known-answer tests that compare every output byte. "Logic" means the algorithm is real but rests on host data or a property the runtime does not give. "Stub" means the member traps with `unsupported: ...` and carries `@stub`.

| Item (spec section) | Tier | Vectors and notes | Status |
| --- | --- | --- | --- |
| Base contracts (2.0) | contracts | toy implementations | implemented |

## Runtime needs

Each stub below waits on a host operation that the runtime does not yet offer. A runtime lane is to add it. The library keeps the full API and logic, so only the body of the host call changes.

| Stub | Host operation needed |
| --- | --- |

## Decisions taken without the owner

1. `OutLen` became the static requirement `output_length()`, because Neura has no value type parameters.
2. `Signer` and `Verifier` are named by their signature type, and a KEM by its ciphertext type, because one class may not implement two instances of `CryptographicPrimitive` (`E0503`).
3. Tests live in one area per package, `tests/stdlib/crypto-<package>/`, with each package's generated vectors under its `support/`. They are not in a single `tests/stdlib/crypto/`. Each area is a separate ctest entry, so the packages run in parallel.
