# `Std.Crypto.Enclave.MemoryKeyStore`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.MemoryKeyStore` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MemoryKeyStore** · [`export let MemoryKeyHandle = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L34)
  A versioned key capability scoped to the store that issued it. Tagged `#Final`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyHandle** · [`export last label: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L40)
  The key's textual label. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyHandle** · [`export last version: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L45)
  The key's version under this label. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyHandle** · [`export last algorithm: Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L50)
  The key's algorithm. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyHandle** · [`export let init = func(store: Owner, name: string, generation: uint32, use: Algorithm)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L53)
- **MemoryKeyStore.MemoryKeyHandle** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L63)
  Whether the handles identify the same key in the same store. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyHandle** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L74)
  The hash of the store identity, label and version. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyHandle** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L80)
  A fixed placeholder that hides key handles and labels. Since 0.3.0.
- **MemoryKeyStore** · [`export let MemoryKeyStore = class implements VersionedKeyStore<MemoryKeyHandle>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L113)
  An isolated in-memory store. An instance belongs to one task at a time. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let provider_name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L121)
  The provider name `memory`. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let generate_key = func(algorithm: Algorithm, label: string) -> Result<MemoryKeyHandle>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L126)
  Generates a fresh key using the operating system. Returns entropy or unsupported-algorithm errors. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let generate_key_with_drbg = func(algorithm: Algorithm, label: string, generator: drbg.ChaCha20Drbg) -> Result<MemoryKeyHandle>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L135)
  Generates a fresh key with a caller-supplied generator. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let import_key = func(algorithm: Algorithm, label: string, material: uint8[list]) -> Result<MemoryKeyHandle>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L145)
  Imports a private copy and issues the next version. AES-SIV expects a 64-byte key and the other supported algorithms expect 32 bytes. A wrong length or malformed label traps. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let resolve = func(label: string, version: uint32) -> Result<MemoryKeyHandle>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L201)
  Resolves one live version under an exact label. Deleted or unknown keys return crypto.key; policy denial returns crypto.policy. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let sign = func(key: MemoryKeyHandle, message: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L216)
  Ed25519 signature or HMAC-SHA-256 under a live signing key. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let wrap_key = func(key: MemoryKeyHandle, data_key: uint8[list], associated_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L233)
  AES-SIV wrapping with the supplied associated data under a live wrapping key. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let unwrap_key = func(key: MemoryKeyHandle, wrapped: uint8[list], associated_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L242)
  Authenticates and unwraps a data key under a live wrapping key. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let delete_key = func(key: MemoryKeyHandle) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L251)
  Wipes the stored copy, removes the live key and retires that version. Tagged `#NoDiscard`. Since 0.3.0.
- **MemoryKeyStore.MemoryKeyStore** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/memory-key-store.nra#L265)
  A fixed placeholder for the store contents. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
