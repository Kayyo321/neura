# `Std.Net.Tls.DynamicCertResolver`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Tls.DynamicCertResolver`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Tls.DynamicCertResolver`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DynamicCertResolver** · [`export let CertificateBundle = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L36)
  A certificate chain, its private key and a CA trust bundle, as PEM text. Tagged `#Final`, `#Immutable`.
- **DynamicCertResolver.CertificateBundle** · [`export last chain_pem: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L38)
  The leaf certificate first, then intermediates (PEM).
- **DynamicCertResolver.CertificateBundle** · [`export last private_key_pem: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L40)
  The private key of the leaf (PEM). Never shown by `to_string`.
- **DynamicCertResolver.CertificateBundle** · [`export last trust_pem: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L42)
  The CA certificates trusted for peers (PEM); empty when none.
- **DynamicCertResolver.CertificateBundle** · [`export let init = func(chain: string, key: string, trust: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L45)
  Use `bundle`, which checks the PEM structure.
- **DynamicCertResolver.CertificateBundle** · [`export let chain_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L53)
  The number of certificates in the chain. Tagged `#Pure`.
- **DynamicCertResolver.CertificateBundle** · [`export let trust_anchor_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L57)
  The number of trusted CA certificates. Tagged `#Pure`.
- **DynamicCertResolver.CertificateBundle** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L60)
  Equal when the chain, key and trust bundle are.
- **DynamicCertResolver.CertificateBundle** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L69)
  A hash of the chain and trust bundle (not the key), consistent with equals.
- **DynamicCertResolver.CertificateBundle** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L72)
  Text such as "CertificateBundle(2 certificates, key redacted, 1 trust anchors)".
- **DynamicCertResolver** · [`export let DynamicCertResolver = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L78)
  Rotates a `CertificateBundle` when the injected change detector reports new material. Tagged `#Final`, `#NotThreadSafe`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let init = func(first: CertificateBundle, load: func() -> Result<CertificateBundle>, detect: func() -> Result<bool>, source: Clock, check_interval: Duration, now: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L97)
  Use `create`, which loads the first bundle.
- **DynamicCertResolver.DynamicCertResolver** · [`export let current = func -> CertificateBundle`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L109)
  The bundle in use now. Tagged `#NoDiscard`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let generation = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L113)
  How many times the bundle was replaced since creation. Tagged `#Pure`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let poll = func -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L119)
  When the check interval has passed since the last check, asks the detector and reloads on a change. Ok(true) when the bundle was replaced; Err when the clock, the detector or the reload failed (the current bundle stays). Tagged `#NoDiscard`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let reload = func -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L137)
  Loads and checks a bundle now and swaps it in. Ok(false) when it equals the current one. Tagged `#NoDiscard`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L151)
  Text such as "DynamicCertResolver(generation 2, CertificateBundle(...))".
- **DynamicCertResolver** · [`export let bundle = func(chain: string, key: string, trust: string) -> Result<CertificateBundle>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L188)
  A bundle from PEM text: the chain must hold at least one complete certificate block, the key a `PRIVATE KEY` block, and a non-empty trust bundle at least one certificate block. Else a Custom error with the code `tls.certificate`. Tagged `#NoDiscard`.
- **DynamicCertResolver** · [`export let create = func(loader: func() -> Result<CertificateBundle>, changed: func() -> Result<bool>, clock: Clock, check_interval: Duration) -> Result<DynamicCertResolver>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L194)
  A resolver whose first bundle comes from `loader` now. A negative interval traps with "invalid argument: interval". Err when the clock, the loader or the first bundle fails. Tagged `#NoDiscard`.
- **DynamicCertResolver** · [`export let watch_paths = func(resolver: DynamicCertResolver, paths: string[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L209)
  Adds file metadata change detection to `resolver.poll`. Registration replaces any prior watch. Each check needs the fs capability and compares file size and modification time. A failed check or reload keeps the active bundle and last successful snapshot. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

`watch_paths` adds file size and modification time to the caller-driven `poll` check. Registration and polling need the file capability. Unreadable or invalid replacements keep the current bundle and can be retried after the next poll interval.

See [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) and the source comments for the complete module contract.
