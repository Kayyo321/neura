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
| `Secret<T>`, `SecretBytes`, `SecretKey<Alg>` (2.1): redacted text, explicit `wipe`, constant-time comparison, no content `equals` | logic | unit tests | implemented |
| Page locking, swap exclusion and zeroization on drop (2.1) | stub (`lock`) | not provided by this runtime | stub |
| `ct_eq`, `ct_select`, `ct_less` (2.1) | logic | edge values (0, 2^32-1, neighbours) | implemented; branch-free in source; the runtime does not guarantee constant time |
| `CryptoPolicy` `Modern`, `Fips140_3`, `Cnsa2_0` (2.1) | logic | table tests and a trap | implemented at run time; compile-time enforcement is outside 0.3.0 |
| SHA-224, SHA-256, SHA-384, SHA-512, SHA-512/256 (2.3) | KAT | Python `hashlib`: 26 messages per digest around every block and padding boundary; streaming in pieces | implemented; `Sha256` shares the kernel of `Std.Digest.Sha256` |
| SHA3-224/256/384/512, SHAKE128/256 (2.3) | KAT | `hashlib`, including lengths rate - 1, rate and rate + 1 for every rate; multi-block squeeze | implemented |
| XOFs as readers (2.3) | logic | `squeeze`, `read` and `reader()` give one stream | implemented |
| cSHAKE128/256, TupleHash128/256 and their XOFs (2.3) | KAT | every sample of NIST's SP 800-185 example files (4 cSHAKE, 12 TupleHash) | implemented |
| TurboSHAKE128/256 (2.3) | KAT | RFC 9861 section 5 (cases up to 17^3 bytes, all domain bytes given) | implemented |
| BLAKE3 hash, keyed hash, derive_key, XOF (2.3) | KAT | official `test_vectors.json` at commit 657284b1, all 24 inputs up to 4097 bytes, 131 output bytes per mode | implemented; serial |
| Multi-threaded SIMD BLAKE3, SHA-NI/AVX-512 acceleration (2.3) | not provided | the serial code gives the same results | not provided by this runtime |
| `MerkleTree<H>` with inclusion and consistency proofs (2.3) | logic | independent Python reference of RFC 6962 section 2.1 (`gen-merkle.py`), sizes 0 to 13, SHA-256 and SHA3-256; verification against tampered proofs | implemented |
| HMAC over every hash: SHA-2 and SHA-3 (2.4) | KAT | Python `hmac`: 5 key and message shapes per digest (keys shorter than, equal to and longer than the block) | implemented |
| KMAC128/256 and KMACXOF (2.4) | KAT | every NIST SP 800-185 KMAC and KMACXOF sample, and 8 `openssl mac` cases | implemented |
| Poly1305 (2.4) | KAT | `cryptography` 43, including all-ones keys and messages for the final carry | implemented |
| AES-CMAC (2.4) | KAT | `cryptography` 43, 128/192/256-bit keys, 0 to 100 bytes | implemented |
| Constant-time verification that refuses truncated tags (2.4) | logic | `verify` tests with truncated and altered tags | implemented |
| HKDF (2.8) | KAT | `cryptography` 43 over SHA-224/256/384/512 and SHA3-256: 15 cases, including empty salt and info, outputs up to 3 × HashLen + 5; PRK from Python `hmac` | implemented |
| PBKDF2 (2.8) | KAT | `hashlib.pbkdf2_hmac`: 20 cases over 5 hashes, dkLen up to 100, up to 10 iterations | implemented |
| scrypt (2.8) | KAT | `hashlib.scrypt`: 5 cases, including RFC 7914's ("", "", 16, 1, 1, 64), r = 3, p = 3 | implemented |
| Argon2id (2.8) | KAT | RFC 9106 section 5.3 (32 KiB, t = 3, p = 4, with secret and associated data) and 6 `openssl kdf ARGON2ID` cases: m 8 to 40, t 1 to 3, p 1 to 3, tags 4 to 100 bytes | implemented |
| Balloon hashing (2.8) | logic | no standard vectors exist; an independent Python reference from the paper's Algorithm 1 (`balloon_reference.py`), 5 cases over SHA-256, SHA-512 and SHA3-256 | implemented |
| PHC strings, `needs_rehash`, password hashing and verification (2.8) | logic | round trips, 11 rejections, verification against an `openssl` tag | implemented |
| Calibration to a target latency (2.8) | logic over host data | a deterministic test clock | implemented over a caller-supplied `Std.Time` clock |

## Runtime needs

Each stub below waits on a host operation that the runtime does not yet offer. A runtime lane is to add it. The library keeps the full API and logic, so only the body of the host call changes.

| Stub | Host operation needed |
| --- | --- |
| `SecretBytes.lock`, `Secret.lock` (and `SecretKey.lock`) | `neura.memory.lock` (new host service, version 1): arguments: the handle of a heap object holding a byte list; results: `Ok` when the runtime has moved the list's storage to locked pages (`mlock`/`VirtualLock`), excluded from core dumps, and promises to zero the storage when the object is reclaimed or the run ends, or `Error(uint32)` with code 12 (out of memory or lock limit) or 14 (capability denied). The collector must then not copy the storage. Capability group: a new `memory` group, granted by default. Record/replay: the call has no observable result besides success, so a trace records only the status. |

## Decisions taken without the owner

1. `OutLen` became the static requirement `output_length()`, because Neura has no value type parameters.
2. `Signer` and `Verifier` are named by their signature type, and a KEM by its ciphertext type, because one class may not implement two instances of `CryptographicPrimitive` (`E0503`).
3. Tests live in one area per package, `tests/stdlib/crypto-<package>/`, with each package's generated vectors under its `support/`. They are not in a single `tests/stdlib/crypto/`. Each area is a separate ctest entry, so the packages run in parallel.
4. Balloon hashing fixes the encodings the paper leaves open: 8-byte little-endian integers, concatenated hash inputs, and `to_int` as the first 8 digest bytes little-endian modulo `space_cost`.
5. Only Argon2id is offered, not Argon2d or Argon2i. `needs_rehash` asks for a rehash on any parameter difference, not only on an upgrade. PHC strings cover argon2id and scrypt, not passlib's PBKDF2 form.
6. Calibration takes a `Clock` and tunes memory before passes, as RFC 9106, section 4 recommends. It stops at 10 passes.
