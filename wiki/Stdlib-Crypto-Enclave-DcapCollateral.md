# `Std.Crypto.Enclave.DcapCollateral`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Enclave.DcapCollateral` to use this module. The [package specification](Stdlib-Design#2112-stdcryptoenclave) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DcapCollateral** · [`export let TcbStatus = enum ( UpToDate, SwHardeningNeeded, ConfigurationNeeded, ConfigurationAndSwHardeningNeeded, OutOfDate, OutOfDateConfigurationNeeded )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L41)
  Intel platform, QE, or TDX module TCB statuses the caller can explicitly allow. Revoked always fails. Since 0.3.0.
- **DcapCollateral** · [`export let SignedJson = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L75)
  A signed Intel collateral body and its directly Intel-rooted signing certificate. Tagged `#Final`. Since 0.3.0.
- **DcapCollateral.SignedJson** · [`export last body: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L79)
  Exact UTF-8 JSON signed by Intel, without its outer response envelope. Since 0.3.0.
- **DcapCollateral.SignedJson** · [`export let init = func(payload: string, signature_bytes, signer_certificate: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L86)
  Copies the 64-byte signature and signing certificate DER alongside the signed JSON text. Since 0.3.0.
- **DcapCollateral.SignedJson** · [`export let signature_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L95)
  A copy of the fixed-width P-256 signature. Tagged `#NoDiscard`. Since 0.3.0.
- **DcapCollateral.SignedJson** · [`export let certificate_der = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L100)
  A copy of the signing certificate. Tagged `#NoDiscard`. Since 0.3.0.
- **DcapCollateral.SignedJson** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L105)
  Whether the signed content, signature and signing certificate are equal. Since 0.3.0.
- **DcapCollateral.SignedJson** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L117)
  A hash of the signed body and supporting bytes. Since 0.3.0.
- **DcapCollateral.SignedJson** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L131)
  The type without the signed body. Since 0.3.0.
- **DcapCollateral** · [`export let SgxCollateralVerifier = class implements dcap.CollateralVerifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L813)
  Validates SGX v3 collateral for Dcap.verify. This profile supports standard SGX PCKs, TCB Info v3 of type zero, QE Identity v2 and full root and issuer CRLs. Tagged `#Final`. Since 0.3.0.
- **DcapCollateral.SgxCollateralVerifier** · [`export let init = func(tcb_info: SignedJson, qe_identity: SignedJson, issuer_der, root_list, pck_list: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L820)
  Copies the exact collateral bytes. The default policy accepts only UpToDate. An explicit allowlist applies to each selected TCB status. Revocation always fails. Since 0.3.0.
- **DcapCollateral.SgxCollateralVerifier** · [`export let verify = func(quote: dcap.Quote, pck_der: uint8[list], trusted_root_der: uint8[list], at: instant.Instant) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L830)
  Validates collateral for the quote and PCK previously authenticated by Dcap.verify. Tagged `#NoDiscard`. Since 0.3.0.
- **DcapCollateral.SgxCollateralVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L836)
  The verifier type without the collateral bytes. Since 0.3.0.
- **DcapCollateral** · [`export let TdxCollateralVerifier = class implements dcap.CollateralVerifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L844)
  Validates TDX v4 collateral for Dcap.verify. This profile supports scalable PCKs, TDX TCB Info v3 of type zero, TD_QE Identity v2 and full root and issuer CRLs. It checks the selected SEAM module signer, attributes and security version. Tagged `#Final`. Since 0.3.0.
- **DcapCollateral.TdxCollateralVerifier** · [`export let init = func(tcb_info: SignedJson, qe_identity: SignedJson, issuer_der, root_list, pck_list: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L851)
  Copies the exact collateral bytes. The default policy accepts only UpToDate. An explicit allowlist applies to each selected TCB status. Revocation always fails. Since 0.3.0.
- **DcapCollateral.TdxCollateralVerifier** · [`export let verify = func(quote: dcap.Quote, pck_der: uint8[list], trusted_root_der: uint8[list], at: instant.Instant) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L861)
  Validates collateral for the quote and PCK previously authenticated by Dcap.verify. Tagged `#NoDiscard`. Since 0.3.0.
- **DcapCollateral.TdxCollateralVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/enclave/dcap-collateral.nra#L867)
  The verifier type without the collateral bytes. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
