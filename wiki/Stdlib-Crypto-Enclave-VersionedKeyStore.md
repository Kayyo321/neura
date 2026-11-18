# `Std.Crypto.Enclave.VersionedKeyStore`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.VersionedKeyStore` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/versioned-key-store.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **VersionedKeyStore** · [`export let VersionedKeyStore = interface<Handle>:KeyStoreProvider<Handle>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/versioned-key-store.nra#L13)
  A provider that resolves a key label and a nonzero version to the same key for its entire lifetime. Its wrapping keys accept 32-byte data keys, authenticate the supplied associated data, and return an independent copy of any unwrapped key. The caller may overwrite that copy. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
