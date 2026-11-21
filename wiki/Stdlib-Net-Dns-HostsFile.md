# `Std.Net.Dns.HostsFile`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.HostsFile`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.HostsFile`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **HostsFile** · [`export const DefaultPath = "/etc/hosts"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L29)
  The usual location of the host table on Linux and macOS. Windows keeps its table at `%SystemRoot%/System32/drivers/etc/hosts` (usually `C:/Windows/System32/drivers/etc/hosts`), and a Windows program passes that path to `read`. On Windows this constant names `/etc/hosts` on the current drive, which normally does not exist, so reading it fails with NotFound.
- **HostsFile** · [`export let HostsEntry = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L33)
  One accepted line: an address and its names, the canonical name first. Tagged `#Final`.
- **HostsFile.HostsEntry** · [`export last address: ips.IpAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L35)
  The address of the line.
- **HostsFile.HostsEntry** · [`export last names: string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L37)
  The canonical name followed by the aliases, as written (trailing dot dropped).
- **HostsFile.HostsEntry** · [`export let init = func(ip: ips.IpAddr, host_names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L40)
  An entry for `ip` with `host_names` (at least one).
- **HostsFile.HostsEntry** · [`export let canonical_name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L47)
  The first name on the line. Tagged `#NoDiscard`.
- **HostsFile.HostsEntry** · [`export let has_name = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L51)
  Whether `name` is one of the entry's names, ASCII case-insensitively, a trailing dot ignored. Tagged `#NoDiscard`.
- **HostsFile.HostsEntry** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L62)
  Equal when the addresses are equal and the names are equal in order (case-sensitively).
- **HostsFile.HostsEntry** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L78)
  Combines the address hash and the name hashes.
- **HostsFile.HostsEntry** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L87)
  The line form, such as "127.0.0.1 localhost loopback".
- **HostsFile** · [`export let HostsProblem = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L93)
  A skipped line: its 1-based number, its text, and why it was skipped. Tagged `#Final`.
- **HostsFile.HostsProblem** · [`export last line: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L95)
  The 1-based line number.
- **HostsFile.HostsProblem** · [`export last text: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L97)
  The line as read, without its end-of-line characters.
- **HostsFile.HostsProblem** · [`export last reason: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L99)
  Why the line was skipped.
- **HostsFile.HostsProblem** · [`export let init = func(number: int, line_text: string, why: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L102)
  A problem on line `number`.
- **HostsFile.HostsProblem** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L109)
  Equal when the line number, text and reason are equal.
- **HostsFile.HostsProblem** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L117)
  Combines the line number, text and reason.
- **HostsFile.HostsProblem** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L121)
  Text such as "line 3: invalid address".
- **HostsFile** · [`export let HostsFile = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L126)
  A parsed host table: its entries in file order and the lines it skipped. Tagged `#Final`.
- **HostsFile.HostsFile** · [`export let init = func(accepted: HostsEntry[list], rejected: HostsProblem[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L133)
  A table of `accepted` entries and `rejected` lines.
- **HostsFile.HostsFile** · [`export let entries = func -> HostsEntry[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L140)
  The accepted entries in file order. Tagged `#NoDiscard`.
- **HostsFile.HostsFile** · [`export let problems = func -> HostsProblem[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L144)
  The skipped lines in file order. Tagged `#NoDiscard`.
- **HostsFile.HostsFile** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L148)
  The number of accepted entries. Tagged `#NoDiscard`.
- **HostsFile.HostsFile** · [`export let lookup = func(name: string) -> ips.IpAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L152)
  Every address listed for `name`, in file order, both families; empty when none. Tagged `#NoDiscard`.
- **HostsFile.HostsFile** · [`export let lookup_family = func(name: string, wanted: AddressFamily) -> ips.IpAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L164)
  The addresses of `name` of one family (`AddressFamily.Ipv4` or `Ipv6`), in file order. Tagged `#NoDiscard`.
- **HostsFile.HostsFile** · [`export let lookup_v4 = func(name: string) -> ips.IpAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L176)
  The IPv4 addresses of `name`. Tagged `#NoDiscard`.
- **HostsFile.HostsFile** · [`export let lookup_v6 = func(name: string) -> ips.IpAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L180)
  The IPv6 addresses of `name`. Tagged `#NoDiscard`.
- **HostsFile.HostsFile** · [`export let reverse = func(ip: ips.IpAddr) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L185)
  Every name listed for `ip` (equal addresses, zone included), in file order, canonical names first within each line; empty when none. Tagged `#NoDiscard`.
- **HostsFile.HostsFile** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L198)
  Equal when the entries and the problems are equal, in order.
- **HostsFile.HostsFile** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L215)
  Combines the hashes of the entries and the problems, consistent with equals.
- **HostsFile.HostsFile** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L227)
  Text such as "HostsFile(3 entries, 1 problems)".
- **HostsFile** · [`export let empty = func -> HostsFile`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L233)
  A table with no entries. Tagged `#NoDiscard`.
- **HostsFile** · [`export let parse = func(text: string) -> HostsFile`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L237)
  Parses host-table text; never fails. Invalid lines are skipped and listed in `problems`. Tagged `#NoDiscard`.
- **HostsFile** · [`export let read = func(path: string) -> Result<HostsFile>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L281)
  Reads and parses the file at `path` (usually `DefaultPath`, or the Windows location it names). Fails only when the file cannot be read as UTF-8 text. Tagged `#NoDiscard`.
- **HostsFile** · [`export let name_problem = func(name: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/hosts-file.nra#L288)
  Why `name` is not a valid host-table name, or "" when it is. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
