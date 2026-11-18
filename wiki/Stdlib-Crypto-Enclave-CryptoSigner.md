# `Std.Crypto.Enclave.CryptoSigner`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.CryptoSigner` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/crypto-signer.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CryptoSigner** · [`export let CryptoSigner = class<Handle> implements Signer<Handle, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/crypto-signer.nra#L14)
  Adapts a provider and a handle to the standard Signer contract. Tagged `#Final`. Since 0.3.0.
- **CryptoSigner.CryptoSigner** · [`export let init = func(provider: KeyStoreProvider<Handle>, handle: Handle, selected: Algorithm)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/crypto-signer.nra#L22)
  Binds a handle to the provider and algorithm that issued it. Since 0.3.0.
- **CryptoSigner.CryptoSigner** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/crypto-signer.nra#L32)
  The algorithm of the provisioned key. Since 0.3.0.
- **CryptoSigner.CryptoSigner** · [`export let try_sign = func(message: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/crypto-signer.nra#L37)
  Signs through the provider and returns any provider error. Tagged `#NoDiscard`. Since 0.3.0.
- **CryptoSigner.CryptoSigner** · [`export let sign = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/crypto-signer.nra#L42)
  Signs through the provider. A provider failure traps. Call try_sign to handle it. Tagged `#NoDiscard`. Since 0.3.0.
- **CryptoSigner.CryptoSigner** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/crypto-signer.nra#L57)
  Redacts the key handle. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
