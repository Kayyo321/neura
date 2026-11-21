# `Std.Net.Dns.ResolvConf`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.ResolvConf`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.ResolvConf`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ResolvConf** · [`export const DefaultPath = "/etc/resolv.conf"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L38)
  The usual location of the file on Linux and macOS. On Windows this constant names `/etc/resolv.conf` on the current drive, which normally does not exist, so reading it fails with NotFound.
- **ResolvConf** · [`export const MaxNameservers: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L40)
  MAXNS: the most name servers used.
- **ResolvConf** · [`export const DefaultNdots: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L42)
  The default `ndots`.
- **ResolvConf** · [`export const DefaultTimeoutSeconds: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L44)
  The default `timeout`, in seconds (RES_TIMEOUT).
- **ResolvConf** · [`export const DefaultAttempts: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L46)
  The default `attempts` (RES_DFLRETRY).
- **ResolvConf** · [`export const MaxNdots: int = 15`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L48)
  The cap of `ndots`.
- **ResolvConf** · [`export const MaxTimeoutSeconds: int = 30`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L50)
  The cap of `timeout`.
- **ResolvConf** · [`export const MaxAttempts: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L52)
  The cap of `attempts`.
- **ResolvConf** · [`export const DnsPort: int = 53`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L54)
  The DNS port used by `nameserver_addresses` by default.
- **ResolvConf** · [`export let ResolvConf = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L59)
  A parsed configuration. Fields are public for reading; build one with `parse`, `defaults`, or the constructor. Tagged `#Final`.
- **ResolvConf.ResolvConf** · [`export last ndots: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L65)
  The `ndots` threshold.
- **ResolvConf.ResolvConf** · [`export last timeout_seconds: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L67)
  The per-try timeout in seconds.
- **ResolvConf.ResolvConf** · [`export last attempts: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L69)
  How many times each server is tried.
- **ResolvConf.ResolvConf** · [`export last rotate: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L71)
  Whether queries rotate among the servers.
- **ResolvConf.ResolvConf** · [`export last edns0: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L73)
  Whether EDNS(0) is used.
- **ResolvConf.ResolvConf** · [`export let init = func(nameservers: ips.IpAddr[list], search: string[list], dots: int, timeout: int, tries: int, round_robin: bool, use_edns0: bool, unknown_options: string[list], problems: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L81)
  A configuration with every value given. Values above the caps are capped and negative values trap with "invalid argument: resolver option".
- **ResolvConf.ResolvConf** · [`export let nameservers = func -> ips.IpAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L99)
  The configured name servers, in file order; empty when the file names none. Tagged `#NoDiscard`.
- **ResolvConf.ResolvConf** · [`export let effective_nameservers = func -> ips.IpAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L103)
  The servers to query: the configured ones, or 127.0.0.1 when there are none. Tagged `#NoDiscard`.
- **ResolvConf.ResolvConf** · [`export let nameserver_addresses = func(port: int) -> SocketAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L114)
  The effective name servers as socket addresses on `port` (usually `DnsPort`). Tagged `#NoDiscard`.
- **ResolvConf.ResolvConf** · [`export let search = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L124)
  The search list (a `domain` line gives a one-entry list). Tagged `#NoDiscard`.
- **ResolvConf.ResolvConf** · [`export let domain = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L128)
  The first search domain, or "" when the list is empty. Tagged `#NoDiscard`.
- **ResolvConf.ResolvConf** · [`export let unknown_options = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L132)
  Options the parser did not know, as written. Tagged `#NoDiscard`.
- **ResolvConf.ResolvConf** · [`export let problems = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L136)
  The skipped lines and values, each "line N: reason". Tagged `#NoDiscard`.
- **ResolvConf.ResolvConf** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L140)
  Equal when the servers, search list and options are equal (unknown options and problems are not compared).
- **ResolvConf.ResolvConf** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L161)
  Combines the compared fields.
- **ResolvConf.ResolvConf** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L176)
  Text such as "ResolvConf(nameservers [10.0.0.1], search [corp.example], ndots 1, timeout 5, attempts 2)", with " rotate" and " edns0" when set.
- **ResolvConf** · [`export let defaults = func -> ResolvConf`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L196)
  The configuration of an empty file: no servers, empty search list, default options. Tagged `#NoDiscard`.
- **ResolvConf** · [`export let parse = func(text: string) -> ResolvConf`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L201)
  Parses resolv.conf text; never fails. Skipped lines and values are listed in `problems`. Tagged `#NoDiscard`.
- **ResolvConf** · [`export let read = func(path: string) -> Result<ResolvConf>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/resolv-conf.nra#L295)
  Reads and parses the file at `path` (usually `DefaultPath`). Fails only when the file cannot be read as UTF-8 text. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
