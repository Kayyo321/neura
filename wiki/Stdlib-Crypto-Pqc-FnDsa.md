# `Std.Crypto.Pqc.FnDsa`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pqc.FnDsa` to use this module. The [package specification](Stdlib-Design#219-stdcryptopqc) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/fn-dsa.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **FnDsa** · [`export let ParameterSet = enum (FnDsa512, FnDsa1024)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/fn-dsa.nra#L17)
  Placeholder names for two FN-DSA parameter sets, after the Falcon-512 and Falcon-1024 parameter sets of the Falcon submission; FIPS 206 may define them differently. Since 0.3.0.
- **FnDsa** · [`export let generate_key_pair = func(set: ParameterSet) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/fn-dsa.nra#L23)
  An FN-DSA key pair: the private key's encoding. Tagged `#NoDiscard`. Since 0.3.0.
- **FnDsa** · [`export let sign = func(set: ParameterSet, private_key: uint8[list], message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/fn-dsa.nra#L32)
  The FN-DSA signature of the message under the encoded private key. Tagged `#NoDiscard`. Since 0.3.0.
- **FnDsa** · [`export let verify = func(set: ParameterSet, public_key: uint8[list], message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/fn-dsa.nra#L41)
  Whether the signature is valid for the message under the encoded public key. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
