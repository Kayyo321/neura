# `Std.Crypto.Enclave.AttestationProvider`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.AttestationProvider` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/attestation-provider.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AttestationProvider** · [`export let AttestationProvider = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/attestation-provider.nra#L10)
  A source of vendor-encoded attestation evidence. The device must bind the caller's report data. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
