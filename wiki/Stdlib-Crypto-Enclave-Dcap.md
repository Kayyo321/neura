# `Std.Crypto.Enclave.Dcap`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.Dcap` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Dcap** · [`export let Platform = enum (Sgx, Tdx)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L44)
  The two supported DCAP quote formats. Since 0.3.0.
- **Dcap** · [`export let Quote = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L57)
  The decoded quote and its embedded evidence. Use the verification entry points to establish trust. Tagged `#Final`. Since 0.3.0.
- **Dcap.Quote** · [`export last platform: Platform`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L61)
  The trusted execution environment named by the header. Since 0.3.0.
- **Dcap.Quote** · [`export last certification_type: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L65)
  The Intel PCK certification data type. Since 0.3.0.
- **Dcap.Quote** · [`export let init = func(tee: Platform, kind: int, original, report, qe_bytes, cert_bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L74)
  Stores the parsed quote fields. Wrong report body or QE report width traps. Since 0.3.0.
- **Dcap.Quote** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L89)
  A copy of the original quote. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap.Quote** · [`export let report_body = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L94)
  A copy of the raw application-enclave or trust-domain report body. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap.Quote** · [`export let qe_report = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L99)
  A copy of the raw SGX quoting-enclave report used for collateral validation. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap.Quote** · [`export let certification_data = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L104)
  A copy of the PCK certification data. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap.Quote** · [`export let report_data = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L109)
  A copy of the 64-byte application REPORTDATA field. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap.Quote** · [`export let measurement = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L114)
  The initial measurement, MRENCLAVE for SGX or MRTD for TDX. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap.Quote** · [`export let debug = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L122)
  Whether the application's report indicates debug mode. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap.Quote** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L127)
  Whether the original encodings are equal. Since 0.3.0.
- **Dcap.Quote** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L134)
  A hash of the quote bytes. Since 0.3.0.
- **Dcap.Quote** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L145)
  The platform name. Since 0.3.0.
- **Dcap** · [`export let parse = func(quote: uint8[list]) -> Result<Quote>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L240)
  Parses an SGX v3 or TDX v4 quote, checking declared lengths and refusing trailing bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap** · [`export let verify_signed = func(quote: uint8[list], trusted_root_der: uint8[list], at: instant.Instant) -> Result<Quote>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L365)
  Authenticates the quote and its QE report with a type-5 PCK chain against the supplied root. PCK certificate validity is checked at the caller's instant. This entry point is for signed evidence inspection. The separate verify operation also requires caller-authenticated collateral and a challenge. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap** · [`export let CollateralVerifier = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L377)
  The caller's verifier for authenticated Intel collateral. Check Intel-signed TCB information, QE identity and root and PCK CRLs, their validity at the supplied instant, and their correspondence to the quote and authenticated PCK certificate. For TDX, include the TDX module identity and security SVN. Since 0.3.0.
- **Dcap** · [`export let verify = func(quote: uint8[list], trusted_root_der: uint8[list], at: instant.Instant, expected_platform: Platform, expected_measurement, expected_report_data: uint8[list], collateral: CollateralVerifier) -> Result<Quote>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L388)
  Authenticates the quote, checks the expected platform, initial measurement and challenge-bearing REPORTDATA, rules out debug mode, and requires successful validation by the supplied collateral verifier. The caller supplies a fresh expected REPORTDATA value and the trusted Intel root. Tagged `#NoDiscard`. Since 0.3.0.
- **Dcap** · [`export let request_quote = func(platform: Platform, report_data: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap.nra#L413)
  Retrieves a quote from an Intel quoting device. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
