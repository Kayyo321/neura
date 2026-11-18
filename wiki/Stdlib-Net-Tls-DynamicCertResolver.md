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

- **DynamicCertResolver** · [`export let CertificateBundle = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L34)
  A certificate chain, its private key and a CA trust bundle, as PEM text. Tagged `#Final`, `#Immutable`.
- **DynamicCertResolver.CertificateBundle** · [`export last chain_pem: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L36)
  The leaf certificate first, then intermediates (PEM).
- **DynamicCertResolver.CertificateBundle** · [`export last private_key_pem: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L38)
  The private key of the leaf (PEM). Never shown by `to_string`.
- **DynamicCertResolver.CertificateBundle** · [`export last trust_pem: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L40)
  The CA certificates trusted for peers (PEM); empty when none.
- **DynamicCertResolver.CertificateBundle** · [`export let init = func(chain: string, key: string, trust: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L43)
  Use `bundle`, which checks the PEM structure.
- **DynamicCertResolver.CertificateBundle** · [`export let chain_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L51)
  The number of certificates in the chain. Tagged `#Pure`.
- **DynamicCertResolver.CertificateBundle** · [`export let trust_anchor_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L55)
  The number of trusted CA certificates. Tagged `#Pure`.
- **DynamicCertResolver.CertificateBundle** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L58)
  Equal when the chain, key and trust bundle are.
- **DynamicCertResolver.CertificateBundle** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L67)
  A hash of the chain and trust bundle (not the key), consistent with equals.
- **DynamicCertResolver.CertificateBundle** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L70)
  Text such as "CertificateBundle(2 certificates, key redacted, 1 trust anchors)".
- **DynamicCertResolver** · [`export let DynamicCertResolver = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L76)
  Rotates a `CertificateBundle` when the injected change detector reports new material. Tagged `#Final`, `#NotThreadSafe`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let init = func(first: CertificateBundle, load: func() -> Result<CertificateBundle>, detect: func() -> Result<bool>, source: Clock, check_interval: Duration, now: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L93)
  Use `create`, which loads the first bundle.
- **DynamicCertResolver.DynamicCertResolver** · [`export let current = func -> CertificateBundle`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L105)
  The bundle in use now. Tagged `#NoDiscard`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let generation = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L109)
  How many times the bundle was replaced since creation. Tagged `#Pure`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let poll = func -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L115)
  When the check interval has passed since the last check, asks the detector and reloads on a change. Ok(true) when the bundle was replaced; Err when the clock, the detector or the reload failed (the current bundle stays). Tagged `#NoDiscard`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let reload = func -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L129)
  Loads and checks a bundle now and swaps it in. Ok(false) when it equals the current one. Tagged `#NoDiscard`.
- **DynamicCertResolver.DynamicCertResolver** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L143)
  Text such as "DynamicCertResolver(generation 2, CertificateBundle(...))".
- **DynamicCertResolver** · [`export let bundle = func(chain: string, key: string, trust: string) -> Result<CertificateBundle>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L161)
  A bundle from PEM text: the chain must hold at least one complete certificate block, the key a `PRIVATE KEY` block, and a non-empty trust bundle at least one certificate block. Else a Custom error with the code `tls.certificate`. Tagged `#NoDiscard`.
- **DynamicCertResolver** · [`export let create = func(loader: func() -> Result<CertificateBundle>, changed: func() -> Result<bool>, clock: Clock, check_interval: Duration) -> Result<DynamicCertResolver>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L167)
  A resolver whose first bundle comes from `loader` now. A negative interval traps with "invalid argument: interval". Err when the clock, the loader or the first bundle fails. Tagged `#NoDiscard`.
- **DynamicCertResolver** · [`export let watch_paths = func(resolver: DynamicCertResolver, paths: string[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/dynamic-cert-resolver.nra#L181)
  Watches files for changes and reloads the resolver when they change. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
