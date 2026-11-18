# `Std.Crypto.Enclave.SevSnp`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.SevSnp` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SevSnp** · [`export let Report = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L30)
  A structurally valid SEV-SNP report with its signed bytes retained. Verify it before trusting any field. Tagged `#Final`. Since 0.3.0.
- **SevSnp.Report** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L36)
  Copies a structurally valid report. For untrusted bytes, call parse. Since 0.3.0.
- **SevSnp.Report** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L47)
  All bytes of this report, including its signature structure. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp.Report** · [`export let report_data = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L52)
  The signed 64-byte report data. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp.Report** · [`export let measurement = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L57)
  The signed 48-byte launch measurement. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp.Report** · [`export let reported_tcb = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L62)
  The reported TCB field. Milan orders its SPLs as bootloader, TEE, four reserved bytes, SNP, microcode. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp.Report** · [`export let vmpl = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L67)
  The reported guest VM privilege level. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp.Report** · [`export let debug_allowed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L72)
  Whether the signed policy allows debugging. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp.Report** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L77)
  Equality by complete report bytes. Since 0.3.0.
- **SevSnp.Report** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L83)
  Hash of the complete report bytes. Since 0.3.0.
- **SevSnp.Report** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L88)
  Displays the report type without displaying chip identification or challenge bytes. Since 0.3.0.
- **SevSnp** · [`export let parse = func(bytes: uint8[list]) -> Result<Report>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L129)
  Parses an untrusted version 2 report and checks its fixed layout and signature padding. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp** · [`export let verify = func(bytes: uint8[list], vcek_der, ask_der, trusted_ark_der: uint8[list], at: time.Instant, expected_data, expected_measurement: uint8[list], minimum_tcb: uint64, allow_debug: bool = false) -> Result<Report>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L199)
  Verifies the Milan ARK/ASK/VCEK chain, the VCEK chip and TCB binding, and the report signature. Checks VMPL 0, the 64-byte expected report data, the 48-byte expected measurement, and a componentwise minimum Milan TCB. Debug policy is denied by default. Supply the certificate DER and provision the trusted ARK independently of the report. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp** · [`export let parse_milan_ca = func(bytes: uint8[list]) -> Result<certs.Certificate>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L255)
  Parses an AMD Milan ASK or ARK, accepting the historical one-bit keyUsage padding. Verification uses the original signed bytes. Authenticate the ARK independently before use. Tagged `#NoDiscard`. Since 0.3.0.
- **SevSnp** · [`export let request_report = func(report_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/sev-snp.nra#L262)
  Requests a fresh report from an AMD SEV-SNP guest device. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
