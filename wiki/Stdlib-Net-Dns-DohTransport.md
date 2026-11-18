# `Std.Net.Dns.DohTransport`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.DohTransport`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.DohTransport`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DohTransport** · [`export const MediaType = "application/dns-message"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L22)
  The media type of DNS messages over HTTP (RFC 8484 section 6).
- **DohTransport** · [`export let DohTransport = class implements transports.DnsTransport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L27)
  A DoH transport: an HTTP client, the endpoint URL (for example "https://dns.example/dns-query"), and the method. Tagged `#Final`, `#NotThreadSafe`.
- **DohTransport.DohTransport** · [`export last url: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L31)
  The endpoint URL without the `dns` parameter.
- **DohTransport.DohTransport** · [`export last use_get: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L33)
  Whether GET is used instead of POST.
- **DohTransport.DohTransport** · [`export let init = func(carrier: http.Client, endpoint: string, get: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L36)
  A transport over `carrier` to `endpoint`, with GET when `get` is true.
- **DohTransport.DohTransport** · [`export let is_stream = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L43)
  A stream transport (HTTP responses are never truncated for size).
- **DohTransport.DohTransport** · [`export let exchange = func(query: messages.Message) -> Result<messages.Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L46)
  One DoH exchange.
- **DohTransport.DohTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L54)
  Text such as "DohTransport(POST https://dns.example/dns-query)".
- **DohTransport** · [`export let wire_query = func(query: messages.Message) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L60)
  The query's wire form with DNS ID 0 (RFC 8484 section 4.1). Tagged `#NoDiscard`.
- **DohTransport** · [`export let get_url = func(endpoint: string, query: messages.Message) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L69)
  The GET URL: `endpoint` plus `?dns=` (or `&dns=` when it has a query) and the base64url text. Tagged `#NoDiscard`.
- **DohTransport** · [`export let get_request = func(endpoint: string, query: messages.Message) -> Result<http.ClientRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L77)
  The GET request. Tagged `#NoDiscard`.
- **DohTransport** · [`export let post_request = func(endpoint: string, query: messages.Message) -> Result<http.ClientRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L85)
  The POST request. Tagged `#NoDiscard`.
- **DohTransport** · [`export let decode_response = func(response: http.Response) -> Result<messages.Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doh-transport.nra#L96)
  The DNS message of a DoH response. Err (Custom `dns.doh_status`) for a status outside 200-299, (`dns.doh_media_type`) for another content type, or the decode error. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
