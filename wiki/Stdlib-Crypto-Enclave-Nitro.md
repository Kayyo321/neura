# `Std.Crypto.Enclave.Nitro`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.Nitro` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Nitro** · [`export let Measurement = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L53)
  A numbered SHA-384 PCR measurement. Tagged `#Final`. Since 0.3.0.
- **Nitro.Measurement** · [`export last index: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L57)
  The PCR number from 0 to 31. Since 0.3.0.
- **Nitro.Measurement** · [`export let init = func(register: int, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L63)
  Copies a 48-byte measurement. A wrong index or length traps. Since 0.3.0.
- **Nitro.Measurement** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L74)
  A copy of the SHA-384 value. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro.Measurement** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L79)
  Whether the PCR numbers and measurements match. Since 0.3.0.
- **Nitro.Measurement** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L89)
  A hash of the index and contents. Since 0.3.0.
- **Nitro.Measurement** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L94)
  The PCR index. Since 0.3.0.
- **Nitro** · [`export let Document = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L100)
  The decoded, signed claims. Use verify to authenticate and check the caller's policy. Tagged `#Final`. Since 0.3.0.
- **Nitro.Document** · [`export last module_id: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L104)
  The issuing Nitro hypervisor module. Since 0.3.0.
- **Nitro.Document** · [`export last timestamp_millis: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L108)
  Signed issuance time in milliseconds after the Unix epoch. Since 0.3.0.
- **Nitro.Document** · [`export let init = func(module_name: string, timestamp: uint64, pcrs: Measurement[list], nonce_bytes, user_bytes, key_bytes: Optional<uint8[list]>, cert, report: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L119)
  Records parsed evidence. Construction alone establishes no trust in the claims. Since 0.3.0.
- **Nitro.Document** · [`export let pcr = func(index: int) -> Optional<Measurement>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L138)
  A PCR by index when the report includes it. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro.Document** · [`export let pcrs = func -> Measurement[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L150)
  A copy of the register list. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro.Document** · [`export let nonce = func -> Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L161)
  A copy of the optional challenge nonce. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro.Document** · [`export let user_data = func -> Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L166)
  A copy of optional signed application data. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro.Document** · [`export let public_key = func -> Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L171)
  A copy of the optional application public key. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro.Document** · [`export let certificate_der = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L176)
  A copy of the infrastructure signing certificate in DER. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro.Document** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L181)
  A copy of the original encoded report. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro.Document** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L186)
  Whether the encodings match. Since 0.3.0.
- **Nitro.Document** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L196)
  A hash of the original encoding. Since 0.3.0.
- **Nitro.Document** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L201)
  A fixed name for the decoded evidence. Since 0.3.0.
- **Nitro** · [`export let parse = func(report: uint8[list]) -> Result<Document>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L450)
  Parses a Nitro report without authenticating its claims. Accepts the optional COSE tag 18, accepts a bounded break-terminated claim map, and caps the report at 32 KiB, CA bundle at eight certificates, and individual DER and optional binary fields at 1024 bytes. Unrecognized or repeated claims fail. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro** · [`export let verify_signed = func(report: uint8[list], trusted_root_der: uint8[list], at: instant.Instant) -> Result<Document>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L488)
  Checks the Nitro COSE ES384 signature and the X.509 path at the explicit instant. The trusted root DER must be provisioned independently by the caller. Use verify for freshness and policy checks. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro** · [`export let verify = func(report: uint8[list], trusted_root_der: uint8[list], at: instant.Instant, expected_nonce: uint8[list], expected_pcrs: Measurement[list], max_age_millis: int64) -> Result<Document>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L589)
  Verifies the chain, signature, freshness, expected nonce, and every provisioned PCR. The maximum age is in milliseconds. Future timestamps, missing or zero PCR0/1/2 (debug evidence), and a nonce or PCR mismatch fail. The expected nonce comes from a fresh verifier challenge. A missing policy input, duplicate expected index, or all-zero PCR policy traps. Comparisons of nonce and measurement bytes are branch-free in source. The runtime does not guarantee constant time. Tagged `#NoDiscard`. Since 0.3.0.
- **Nitro** · [`export let request_attestation = func(nonce, user_data, public_key: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/nitro.nra#L628)
  Requests a report from the Nitro Secure Module. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
