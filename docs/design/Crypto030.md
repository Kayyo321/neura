# `Std.Crypto` in Neura 0.3.0

The base cryptographic contracts, classical and post-quantum algorithms, PKI, tokens, enclave evidence and advanced schemes share the [standard library contract](../lang/Stdlib.md#21-stdcrypto). The tier tables record implemented profiles, verification evidence and outstanding work.

## Purpose

`Std.Crypto` implements the algorithms in Neura. SHA-256 uses the existing `Std.Digest.Sha256` kernel. Known-answer tests compare output bytes against standards or independent implementations. Some operations are branch-free in source. The runtime does not guarantee constant time or erase heap copies of secrets made by the collector.

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
| Dedicated host buffers (2.1) | logic over host services | Linux lock, equality and release, failed capability requests and refusal during trace recording | implemented for retained byte buffers. Generic values require a runtime adapter. |
| `ct_eq`, `ct_select`, `ct_less` (2.1) | logic | edge values (0, 2^32-1, neighbours) | implemented; branch-free in source; the runtime does not guarantee constant time |
| `CryptoPolicy` `Modern`, `Fips140_3`, `Cnsa2_0` (2.1) | logic | table tests and a trap | implemented at run time; compile-time enforcement is outside 0.3.0 |
| OS entropy (`getrandom` and the platform equivalents) (2.2) | logic over host data | allowed and denied capability tests | implemented over the existing `neura.entropy.fill` service |
| Hardware entropy RDSEED/RNDR and fusion with OS entropy (2.2) | stub (`hardware_bytes`, used by `fused_seed`) | The runtime has a hardware entropy service. The public wrapper remains unbound. | stub |
| ChaCha20-DRBG over a seed, reseeding (2.2) | logic | no standard vectors exist for this construction; an independent Python reference (`gen-drbg.py`) over hashlib and `cryptography`'s ChaCha20 (11 outputs over 3 seeds, with reseeds) | implemented |
| Fork and VM-clone detection with immediate reseed (2.2) | stub (`fork_generation`, used by `fork_safe`) | The host process-generation service detects forked children. VM snapshot restores require a host signal. | stub. The reseed logic is implemented. |
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
| AES-128/192/256 block, `AesBlock` (2.5) | KAT | `cryptography` ECB: the FIPS 197 Appendix C plaintext and a pattern block, encrypted and decrypted | implemented |
| AES-GCM with 96-bit nonces (2.5) | KAT | `cryptography`: 18 cases, all three key sizes, 0 to 100 bytes, with and without associated data | implemented |
| AES-GCM-SIV (2.5) | KAT | all 50 vectors of RFC 8452 Appendix C, including the counter-wrap tests, and 8 `cryptography` cases | implemented |
| AES-SIV deterministic AEAD (2.5) | KAT | RFC 5297 A.1 and A.2 and 12 `cryptography` cases: 32-, 48- and 64-byte keys, 0 to 3 associated-data items | implemented |
| ChaCha20, XChaCha20, HChaCha20 (2.5) | KAT | `cryptography`, including a counter near 2^32; draft-irtf-cfrg-xchacha-03 sections 2.2.1 and A.3.2.1 | implemented |
| ChaCha20-Poly1305 (2.5) | KAT | `cryptography`: 6 cases | implemented |
| XChaCha20-Poly1305 (2.5) | KAT | draft-irtf-cfrg-xchacha-03 A.3.1, and 6 cases computed with `cryptography`'s ChaCha20 for HChaCha20 and its ChaCha20-Poly1305 | implemented |
| STREAM online AEAD, `OnlineCipher` (2.5) | logic | no standard vectors exist for this nonce layout; an independent Python reference (`gen-stream.py`) over `cryptography`'s GCM, GCM-SIV, ChaCha20-Poly1305 and XChaCha20-Poly1305; truncated, reordered, appended and altered streams are rejected | implemented |
| FF1 (2.5) | KAT | all 9 NIST FF1 samples (AES-128/192/256, radix 10 and 36, with and without a tweak); radix 2 and 65536 round trips | implemented |
| Raw ECB and CBC modes (2.5) | not exposed | | by design |

### Asymmetric and post-quantum

| Item (spec section) | Tier | Vectors and notes | Status |
| --- | --- | --- | --- |
| X25519, X448, Ed25519 and Ed448 (2.6) | KAT | RFC 7748 and RFC 8032, independent cryptography and PyNaCl, and Wycheproof negative cases. | implemented |
| ECDSA and ECDH (2.6) | KAT | RFC 6979, OpenSSL deterministic signatures, and Wycheproof malformed, wrong-curve and boundary cases. | implemented |
| RSA and HPKE (2.6) | KAT | Independently checked RSA-PSS, RSA-OAEP, PKCS #1 v1.5 and keygen, and official RFC 9180 HPKE vectors. | implemented |
| ML-KEM, ML-DSA and SLH-DSA (2.7) | KAT | Selected NIST ACVP vectors for all parameter sets. ML-KEM adds public input modulus checks and implicit-rejection tests. Long SLH-DSA signing sets run a representative selection. | implemented |
| TLS X25519MLKEM768 and X-Wing (2.7) | KAT | OpenSSL 3.5.7 with independent component tests, plus X-Wing draft -11 Appendix C. TLS rejects all-zero DH contributions. | implemented |
| Composite ML-DSA-65 and Ed25519 (2.7) | KAT | Official -19 signatures with and without context, seed-derived public key, and independent deterministic component signatures. | implemented |
| FN-DSA (2.7) | stub | On November 18, 2026, NIST listed FIPS 206 as in development and had released neither an initial public draft nor matching vectors. [NIST status](https://csrc.nist.gov/projects/post-quantum-cryptography/post-quantum-cryptography-standardization). | stub |

The TLS and X-Wing APIs are separate. TLS concatenates the two secrets after checking the X25519 contribution. X-Wing applies the draft's domain-separated combiner to the raw contribution, including all-zero X25519. The composite signature passes the suite label as the ML-DSA context and requires both signatures. The approved-algorithm allowlist of `Fips140_3` denies all three hybrid constructions and `Hpke`. HPKE has a single policy identifier and also supports nonapproved suites. Allowing it would require a suite-specific policy that covers the whole RFC 9180 construction.

### PKI

Verification runs in Neura and shares the asymmetric and post-quantum primitives. RSA-PSS supports an independent MGF1 hash and enforces the minimum salt length in a constrained key. PKCS #1 v1.5 verification accepts both absent and NULL signature parameters for interoperable SHA-2 and SHA-3 encodings. SHA-1 signatures and policy-denied schemes return UnsupportedAlgorithm. CSOR HashML-DSA keys and signatures must use the matching OID. This is outside the RFC 9881 profile.

| Item (spec section) | Tier | Vectors and notes | Status |
| --- | --- | --- | --- |
| Strict DER reader and writer, OIDs, BIT STRING, strings, times (2.9) | KAT + logic | `openssl asn1parse -genstr` encodings (11 OIDs incl. 2.25 128-bit arcs, 14 INTEGERs, 8 times, 8 string types); 21 malformed inputs and 27 malformed values with exact error offsets; recorded certificates rebuilt byte for byte; fuzz: every truncation of 3 certificates, 2 single-byte mutations per byte of the leaf, hostile lengths; the typed parsers (certificate, CSR, PKCS #8, EncryptedPrivateKeyInfo, OCSP request and response, SCT list, filter cascade, PEM with non-ASCII text) get every truncation and byte mutations, none traps | implemented |
| Strict PEM, several blocks (2.9) | KAT | `openssl x509 -outform PEM` output decoded and re-encoded byte for byte; 14 rejections | implemented |
| X.509 v3 certificates, all RFC 5280 extensions, SCT list kept as bytes (2.9) | KAT | 11 recorded openssl certificates (ECDSA P-256/P-384, Ed25519, RSA PKCS #1, RSA-PSS, ML-DSA-44): every field compared with Python `cryptography` 43 (policyMappings with openssl), 218 field checks | implemented |
| SubjectPublicKeyInfo with algorithm identification (2.9) | KAT | 18 openssl keys: RSA, RSA-PSS, P-256/384/521, secp256k1, Ed25519, Ed448, X25519, X448, ML-DSA-44/65/87, ML-KEM-512/768/1024, SLH-DSA-SHA2-128s, SLH-DSA-SHAKE-128f; OIDs from the NIST CSOR page | implemented |
| PKCS #10 CSRs: parse and build with a caller's signer (2.9) | KAT | 2 openssl CSRs (extensionRequest, challengePassword) parsed against `cryptography` and rebuilt byte for byte | implemented |
| PKCS #8 OneAsymmetricKey (2.9) | KAT | the 18 keys as `openssl pkcs8 -topk8 -nocrypt`, re-encoded byte for byte | implemented |
| EncryptedPrivateKeyInfo, PBES2 with PBKDF2 and AES-CBC/GCM (2.9) | KAT | 4 `openssl pkcs8 -v2` records (one HMAC-SHA-1, refused) decrypted against hashlib + `cryptography`; 5 PBES2 encodings (AES-128/192/256-GCM, AES-128/256-CBC) built here and by hashlib + `cryptography` byte for byte; GCM built here because `openssl pkcs8` refuses AEAD | implemented |
| RFC 5280 section 6.1 path validation with policy tree, name constraints, EKU, RFC 9525 names (2.9) | logic | 199 C2SP x509-limbo cases (commit b12423782a; rfc5280, pathlen, pathological, webpki, cve, invalid, rfc9881 prefixes): all match, 24 documented divergences (webpki/CA-B Forum-only rules, listed in gen-pki-paths.py); recorded openssl chains with one error code per check; 21 policy cases over 4 chains (skip counts, two mapping levels, anyPolicy intermediates, a self-issued rollover) decided by `openssl verify -policy_check`; signatures from a truth table of `cryptography` (and openssl for ML-DSA) results | implemented; signatures through the phase-2 verifier |
| OCSP request/response, delegated responders, nonce (2.9) | KAT + logic | 5 `openssl ocsp` requests rebuilt byte for byte (SHA-1 and SHA-256 CertIDs, nonce, two certificates); 8 responses (good, revoked, unknown, two, delegated by name and by key, no-EKU responder) parsed against `cryptography` and verified; CertID hashes against hashlib | implemented |
| SCTs, precert reconstruction, STH, Merkle proofs (2.9) | KAT + logic | 2 real SCTs of letsencrypt.org (recorded 2026-11-17) with signed data rebuilt and checked against `cryptography`; a generated RSA log: TLS list, embedded SCT from a precertificate, 7-leaf log, 18 audit paths and 7 consistency proofs, STH JSON | implemented |
| CRLite filter cascades (2.9) | KAT | 3 cascades built by mozilla/filter-cascade (b5469a2d) over CRLite keys of recorded certificates; 8 rust-cascade test_data files (f05b3da9) incl. a real v1 CRLite filter, MurmurHash3, SHA-256 counter mode, inverted and salted | implemented |
| RSA PKCS #1 v1.5 and PSS, ECDSA, Ed25519, Ed448, pure ML-DSA and SLH-DSA signature dispatch (2.9) | KAT | Certificates produced and independently verified with OpenSSL 3.5.7 and Python cryptography 43. Covers key mismatches, altered signatures, RSA-PSS salt restrictions and a separate MGF1 digest. | implemented |
| HashML-DSA SHA-512 and composite ML-DSA-65 with Ed25519 (2.9) | KAT | NIST ACVP and OpenSSL HashML-DSA, plus official composite -19 raw and X.509 vectors. | implemented |
| Default verification for certificate paths, CSRs, OCSP and CT (2.9) | logic + KAT | Independent RSA-PSS, ECDSA, and Ed25519 fixtures through the public entry points. | implemented |

### Token

| Item (spec section) | Tier | Vectors and notes | Status |
| --- | --- | --- | --- |
| JWK and compact JWS (2.10) | KAT | RFC 7515 A.1–A.5, RFC 7517 A.1/A.3, RFC 7520 4.1–4.4 and RFC 8037. Algorithm substitution, HMAC/RSA confusion, unsecured tokens, malformed critical headers and tampered tags are refused. | implemented |
| JWT time claims (2.10) | logic + KAT | RFC 7519 claims, an authenticated RFC 7515 token, a fixed clock, and clock capability denial. | implemented |
| Compact JWE direct and AES Key Wrap (2.10) | KAT | RFC 3394, RFC 7516 A.3, RFC 7518 CBC-HMAC, RFC 7520 5.6, and independent Python cryptography checks. Tests include failed authentication and AES-CBC padding failure. | implemented |
| PASETO v4.local and v4.public (2.10) | KAT | All 17 pinned official v4 records. Exact local encryption and public signatures, plus rejection of altered tags, footer and assertion mismatches, wrong purpose, and invalid encodings. | implemented |
| Macaroons v1/v2 wire format (2.10) | KAT | 16 libmacaroons vectors and independent PyMacaroons vectors for first-party and nested third-party caveats. | implemented |
| Compact RSA-OAEP-256 JWE (2.10) | KAT | The RSA key from RFC 7516 A.1, independently generated SHA-256 OAEP and GCM expected bytes, rejection of SHA-1 and a uniform authentication error after bad unwrap. | implemented |
| Biscuit v3 credentials and Datalog v3.0–v3.3 (2.10) | KAT + logic | All 38 pinned Eclipse Biscuit samples, including the explicit external-function example. Rust Biscuit 6.0.0 exact Ed25519 and P-256 mint, attenuation, third-party request and attachment, and seal bytes. Tests cover provenance-aware authorization and stable P-256 revocation across both valid signature representations. | implemented for the bounded v3 profile |

JOSE verification keys bind the caller's choice of algorithm and key family. The header only confirms that choice. Critical header extensions and unencoded JWS payloads are outside this profile. For JWT, integer time claims avoid numeric rounding, and future-issued tokens follow the same leeway as not-before claims. The RSA1_5 and SHA-1 OAEP modes of JWE are refused. PASETO authenticates bytes and leaves claim validation to its caller. Macaroons authenticate the caveat chain and require a caller to decide which first-party predicates hold.

Biscuit ordinary attenuation emits checks. Verification accepts ordinary signed blocks with scoped facts and rules. Third-party blocks require Datalog v3.2 or later and the version 1 previous-signature transcript. The legacy request-key and signature layouts are outside this profile. The wire revocation accessor returns exact raw signatures. Use the stable accessor and synthesized Datalog facts for representation-independent P-256 revocation. The root key ID is metadata and the caller selects the trusted root.

Biscuit regex uses Neura's Pike machine with 128-byte patterns and 1,024-byte subjects. Its character classes and word boundaries use ASCII semantics. Rust-specific regex features including Unicode properties and inline flags are outside this profile. Each authorization supports 16 explicitly supplied callback names and at most 1,024 invocations. Typed operands and results are copied, with up to 32 collection items and eight levels of nesting. Unregistered names fail closed even in lazy expressions.

### Enclave

| Item (spec section) | Tier | Vectors and notes | Status |
| --- | --- | --- | --- |
| Versioned software key store, provider adapter and envelope engine (2.11) | logic + KAT | Python cryptography AES-SIV and AES-GCM, Ed25519 and HMAC. Checks header and AAD, use-count rotation, old-version decrypt, cache clearing, policy changes and wrong or retired KEKs. | implemented |
| AWS Nitro COSE_Sign1 (2.11) | logic + KAT | AWS Root-G1, a signed AWS debug sample, and a published production report with independently recorded nonce and PCRs. | implemented |
| AMD SEV-SNP v2 Milan VCEK (2.11) | logic + KAT | Google go-sev-guest signed report and AMD KDS ARK and ASK. The known report has debugging enabled and is accepted only when the caller opts in. | implemented for this profile |
| Intel DCAP SGX v3 and TDX v4 quotes (2.11) | logic + KAT | Published production captures signed under the official Intel root. Verifies embedded PCK chains, QE signatures and key binding, quote signatures and the caller's measurements and report data. | implemented for this profile |
| Intel DCAP collateral (2.11) | logic + KAT | Exact signed Intel TCB Info v3 and QE Identity v2 with both full signed CRLs. Checks historical production captures and direct Intel PCS v4 snapshots from November 18, 2026. Verifies dates, PCK identity and TCB levels, QE identity and masks, and the TDX module level. Rejects unrecognized collateral and revocation. | standard SGX v3 and scalable TDX v4 implemented. TDX v5, other PCK profiles, advisory-ID policy and freshness rollback tracking are outside this profile. |
| PKCS #11, TPM and cloud KMS provider connections and vendor report or quote requests (2.11) | stub | The engine and offline evidence validation run without these services. | stub |

The envelope checks that its KEK version remains live and allowed by policy even when a DEK is cached. It clears the cache on a missing or denied key and on successful KEK rotation. A failed unwrap returns an authentication error. Deterministic inputs must be unique for each fresh cache epoch. The software AES-SIV wrap is available under `Modern`. Nitro and Milan compatibility relax only known certificate profile constraints while retaining signature, validity, critical-extension and path checks. The Milan ASK parser also permits one exact historical padding pattern in the signed key-usage field. The default certificate parser continues to reject it.

### Advanced

| Item (spec section) | Tier | Vectors and notes | Status |
| --- | --- | --- | --- |
| Shamir GF(2^8) and prime field (2.12) | KAT + logic | HashiCorp Vault reference and an independent integer implementation for 2^521 − 1. Duplicate, short, and mixed-size shares are rejected. | implemented |
| Ristretto255 and SHA-512 hash-to-curve expansion (2.12) | KAT | All RFC 9496 generator, invalid encoding, and uniform map examples, plus RFC 9380 SHA-512 XMD and RFC 9497 hash-to-group checks. | implemented |
| FROST (2.12) | KAT | RFC 9591 Appendix E for Ed25519, Ed448, ristretto255, P-256 and secp256k1. Checks exact key shares, commitments, signature shares and aggregate signatures. Rejects nonce reuse, wrong messages, invalid shares, mixed-suite rosters and inputs, and nonprime-subgroup Edwards points. | implemented for all five RFC ciphersuites |
| RSA blind signatures (2.12) | KAT | All four RFC 9474 Appendix A transcripts, independently checked with Python modular arithmetic and RSA-PSS verification. | implemented |
| OPRF, VOPRF and POPRF (2.12) | KAT | All 40 RFC 9497 single and batch transcripts in Appendix A. RFC 9496 decaf448 canonical and invalid encodings are exercised through the public OPRF API. Rejects reordered or missing replies, altered proofs, changed public info and malformed or cross-suite points. | implemented for all five RFC 9497 ciphersuites |
| P-256, P-384 and P-521 hash to curve (2.12) | KAT | All examples in RFC 9380 J.1.1, J.2.1 and J.3.1. Independent affine map and oversized DST checks cover both SEC1 encodings. | implemented |
| Merlin STROBE transcripts (2.12) | KAT | Rust merlin 3.0.0 checked on forked transcripts, empty and multi-block frames, and variable challenge lengths. SHA-3 family regressions exercise the shared permutation. | implemented |
| Bulletproofs range proofs (2.12) | KAT | Rust bulletproofs 5.0.0 exact single-value commitments and proofs at 8, 16, 32 and 64 bits and aggregate proofs for two 8-bit values and four 16-bit values. Rejects substituted or reordered commitments, invalid aggregate counts, transcript changes and bad proof equations. | single-value and aggregate profiles implemented. Distributed multiparty proving and R1CS are outside this profile. |

Shamir's prime-field encoding is fixed to big-endian integers and 82 coefficient bytes reduced modulo 2^521 − 1. The OS entropy path requests coefficients in chunks within the host limit. Shamir shares do not authenticate a share set. FROST currently uses a trusted dealer and a caller-authenticated roster. One-use client state burns a nonce or blind on a failed protocol attempt as well as a successful one. The RSA blind signer applies its own independent blinding and checks the RSA private operation with the public exponent. FROST validates canonical suite encodings before signing. Imported Edwards points must belong to the prime-order subgroup. The unqualified APIs retain the ristretto255 profile. For OPRF, the suite and mode bind the transcript. The zero-based batch index uses two bytes. Private and public application inputs are shorter than 65,535 bytes under RFC 9497 section 5.1. Key derivation allows up to 65,535 bytes of key info. The `Fips140_3` policy denies the whole OPRF construction. Servers applying RFC 9497 query budgets count every evaluated element, including elements submitted within a batch. Bulletproofs uses the standard Dalek generators and a fork of the caller’s Merlin transcript. Secret scalar multiplication is branch-free in source. The runtime does not guarantee constant time.

## Runtime needs

The following public entries need a runtime service or a binding to a service. The [NVM host service table](../runtime/NVM.md#host-services) specifies the existing service contracts.

`SecretBytes.try_lock` uses `neura.memory.lock.*` on Linux, which excludes dedicated buffers from swap, core dumps and forked children. On success the retained heap list is zeroed and subsequent exposures make ordinary heap copies. On failure the caller keeps its original list and receives the provider error. The host clears a locked buffer on release or when execution ends. A recording run refuses allocation.

| Stub | Host operation needed |
| --- | --- |
| `Entropy.hardware_bytes` | A library binding to the existing `neura.entropy.hardware` service for RDSEED or RNDR. |
| `Entropy.fork_generation` (used by `ChaCha20Drbg.fork_safe`) | A runtime adapter that detects VM snapshot restores as well as forked children. The current `neura.process.generation` service detects a change of process ID after fork. |
| `Secret<T>.lock` | The host services accept dedicated byte buffers. They have no adapter for an arbitrary generic Neura value. |
| `Pkcs11.open`, `Tpm.open`, `CloudKms.open` | Host providers for token sessions, TPM commands, or authenticated KMS communications. They must hold credentials and private keys outside the circuit, issue opaque scoped handles, and implement the versioned lookup and authenticated wrap contracts. |
| `Nitro.request_attestation`, `SevSnp.request_report`, `Dcap.request_quote` | Nitro Secure Module, AMD guest report, or Intel SGX/TDX quote host operation. The application passes explicit challenge bytes and the provider returns the device's signed evidence. Offline verification consumes only the report, collateral, and the caller's trust and time inputs. |

## Class display and equality

Secret-bearing classes use fixed displays. Secret byte and key containers use identity equality. Public HPKE sealed messages and CRLite Bloom layers compare and hash their stored fields. X.509 names give each relative distinguished name multiset equality. Equal text attributes hash by normalized text and equal binary attributes hash by DER. PHC strings retain their canonical password-verifier serialization.

## Edge cases

1. An `update` after a digest's output was read, and a second `finish` or `verify` of a MAC, trap. They never return a value that looks valid.
2. `verify` accepts only a full-length tag, because a truncated tag weakens it without the caller noticing. The lengths of byte lists are treated as public, and only the contents are compared in constant time.
3. A failed AEAD `open` returns an error and never releases plaintext, including for input shorter than the tag.
4. A key, nonce or parameter of the wrong size traps with an `invalid argument:` message, because it is a programming error. A wrong tag, a malformed PHC string or a refused entropy request is a `Result` error, because it depends on data.
5. Primitives work inside spawned tasks. The policy lives in an `AtomicInt64`, and every constant table is built per object, because arrays are not transferable (`E0601` otherwise).
6. A stub traps with `unsupported: ...` instead of returning something that looks like a result. The default paths (`SecretBytes` construction, a seeded `ChaCha20Drbg`, `Entropy.os_bytes`) never reach a stub.

## Order of work and gate

Each package has a CTest area under `tests/stdlib/crypto-*`. The area harness compares two optimized runs, checks an unoptimized run and verifies trap programs in both build modes. CTest runs the areas with an installed standard library. Vector generators in `scripts/crypto-vectors/` write reproducible inputs and expected results.

## Migration

Nothing changes for existing programs. `Std.Digest.Sha256` keeps its API, and `Std.Crypto.Hash.Sha2.Sha256` wraps the same kernel. `Std.Random.Random` stays a non-cryptographic generator. Programs that need secrets move to `Std.Crypto.Random`.

## Decisions taken without the owner

1. `OutLen` became the static requirement `output_length()`, because Neura has no value type parameters.
2. `Signer` and `Verifier` are named by their signature type, and a KEM by its ciphertext type, because one class may not implement two instances of `CryptographicPrimitive` (`E0503`).
3. Tests live in one area per package, `tests/stdlib/crypto-<package>/`, with each package's generated vectors under its `support/`. They are not in a single `tests/stdlib/crypto/`. Each area is a separate ctest entry, so the packages run in parallel.
4. Balloon hashing fixes the encodings the paper leaves open: 8-byte little-endian integers, concatenated hash inputs, and `to_int` as the first 8 digest bytes little-endian modulo `space_cost`.
5. Only Argon2id is offered, not Argon2d or Argon2i. `needs_rehash` asks for a rehash on any parameter difference, not only on an upgrade. PHC strings cover argon2id and scrypt, not passlib's PBKDF2 form.
6. Calibration takes a `Clock` and tunes memory before passes, as RFC 9106, section 4 recommends. It stops at 10 passes.
7. AES-GCM takes only 96-bit nonces. AES-SIV used as an `AeadCipher` puts its associated data and nonce in the item vector `[data, nonce]`, and it reports a nonce length of 0, meaning any length.
8. STREAM's segment nonce (prefix ‖ big-endian `uint32` counter ‖ last-segment byte) and its prefix length (the AEAD's nonce length less 5) are defined here, as Tink's nonce-prefix segmented AEAD does. No standard fixes them.
9. FF1 uses the minimum domain of SP 800-38G Revision 1 (radix^minlen ≥ 10^6). Its text forms return `Result`, and misuse of the numeral API traps.
10. Neither RFC 5297 nor `cryptography` provides an empty-plaintext AES-SIV case, so there is none here. The empty-plaintext GCM-SIV cases come from RFC 8452.
11. `ChaCha20Drbg`'s construction (SHA-256 seeding and reseeding with fixed labels, fast key erasure over ChaCha20 with a zero nonce) is defined here, not taken from a standard. FIPS 140-3 approves only the SP 800-90A generators, so `Fips140_3` and `Cnsa2_0` deny it.
12. OS entropy is not a stub: the runtime already has `neura.entropy.fill`, and `Std.Crypto.Random.Entropy` binds it directly, with requests of up to 1 MiB.
13. The policy tables are this library's reading of the standards. `Fips140_3` allows SHA-2, SHA-3 and SP 800-185, HMAC, KMAC, CMAC, AES, GCM, FF1, HKDF, PBKDF2 and the FIPS 186-5, 203, 204 and 205 schemes. It denies ChaCha20, Poly1305, BLAKE3, AES-SIV, GCM-SIV, scrypt, Argon2id, Balloon, STREAM and ChaCha20-DRBG. `Cnsa2_0` allows only AES-256, SHA-384 and SHA-512, ML-KEM-1024 and ML-DSA-87, and HMAC, HKDF, PBKDF2 and Merkle trees over those hashes. The default is `Modern`.
14. The constant-time helpers are functions over `uint32` and byte lists, not the specification's `CtEq`, `CtSelect` and `CtLess` value types.
15. The library adds contracts and modules the specification does not name: the `Algorithm` enum, `ExtendableOutput` (the XOF contract), `Mac.Authenticator`, `Core.SecretKey<Alg>`, `Core.Secret<T>` with an eraser function, and `Kdf.PasswordHash`.
16. `SecretBytes` and the key classes keep identity `equals` and `hash_code`. Contents are compared only with `ct_equals`.
