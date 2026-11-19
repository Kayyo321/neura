# `Std.Crypto.Enclave.Pkcs11`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.Pkcs11` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/pkcs11.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Pkcs11** · [`export let Provider = interface:versioned.VersionedKeyStore<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/pkcs11.nra#L11)
  An opaque versioned key store for this backend. String handles have meaning only to their provider. Since 0.3.0.
- **Pkcs11** · [`export let open = func(library_path: string, token_label: string) -> Result<Provider>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/pkcs11.nra#L17)
  Opens this configured provider. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
