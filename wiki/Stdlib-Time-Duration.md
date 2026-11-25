# `Std.Time.Duration`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Time.Duration`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Time.Duration`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Duration** · [`export let Duration = class implements Comparable<Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L15)
  An immutable signed duration represented as an exact count of nanoseconds. Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`.
- **Duration.Duration** · [`export let init = func(nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L20)
  Stores the signed nanosecond count.
- **Duration.Duration** · [`export let plus = func(other: Duration) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L26)
  Adds two durations with checked nanosecond arithmetic. Tagged `#Pure`.
- **Duration.Duration** · [`export let minus = func(other: Duration) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L30)
  Subtracts another duration with checked nanosecond arithmetic. Tagged `#Pure`.
- **Duration.Duration** · [`export let multiplied_by = func(factor: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L34)
  Scales the duration by an integer. Overflow traps. Tagged `#Pure`.
- **Duration.Duration** · [`export let divided_by = func(divisor: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L39)
  Truncates toward zero. A zero divisor traps (reason 1), as does the most negative duration divided by -1. Tagged `#Pure`.
- **Duration.Duration** · [`export let negated = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L43)
  Reverses the sign. The most negative duration cannot be negated. Tagged `#Pure`.
- **Duration.Duration** · [`export let abs = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L47)
  Returns the nonnegative magnitude. The most negative duration traps. Tagged `#Pure`.
- **Duration.Duration** · [`export let is_zero = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L53)
  Reports whether the nanosecond count is zero. Tagged `#Pure`.
- **Duration.Duration** · [`export let is_negative = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L57)
  Reports whether the nanosecond count is below zero. Tagged `#Pure`.
- **Duration.Duration** · [`export let to_nanos = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L61)
  Returns the exact signed nanosecond count. Tagged `#Pure`.
- **Duration.Duration** · [`export let to_micros = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L65)
  The to_ conversions truncate toward zero. Tagged `#Pure`.
- **Duration.Duration** · [`export let to_millis = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L69)
  Returns whole milliseconds, truncating toward zero. Tagged `#Pure`.
- **Duration.Duration** · [`export let to_seconds = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L73)
  Returns whole seconds, truncating toward zero. Tagged `#Pure`.
- **Duration.Duration** · [`export let to_seconds_float64 = func -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L78)
  The nanoseconds converted to float64, divided by 10^9. Exact conversion, so correctly rounded, while the count is at most 2^53. Tagged `#Pure`.
- **Duration.Duration** · [`export let compare_to = func(other: Duration) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L82)
  Orders durations by their signed nanosecond counts. Tagged `#Pure`.
- **Duration.Duration** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L91)
  Tests equality of signed nanosecond counts. Reading the argument can run its class hook.
- **Duration.Duration** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L100)
  Hashes the signed nanosecond count. Tagged `#Pure`.
- **Duration.Duration** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L107)
  ISO 8601: PT, then hours, minutes, and seconds with their unit letters, zero units omitted, the fraction trimmed of trailing zeros. Zero is PT0S; a negative duration is - and the text of its magnitude. Tagged `#Pure`.
- **Duration** · [`export const Zero: Duration = new Duration(0)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L133)
  The duration of zero nanoseconds.
- **Duration** · [`export let of_nanos = func(nanos: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L137)
  Constructs a duration from an exact nanosecond count. Tagged `#Pure`, `#ThreadSafe`.
- **Duration** · [`export let of_micros = func(micros: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L141)
  Converts microseconds to nanoseconds. Overflow traps. Tagged `#Pure`, `#ThreadSafe`.
- **Duration** · [`export let of_millis = func(millis: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L145)
  Converts milliseconds to nanoseconds. Overflow traps. Tagged `#Pure`, `#ThreadSafe`.
- **Duration** · [`export let of_seconds = func(seconds: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L149)
  Converts seconds to nanoseconds. Overflow traps. Tagged `#Pure`, `#ThreadSafe`.
- **Duration** · [`export let of_minutes = func(minutes: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L153)
  Converts 60-second minutes to nanoseconds. Overflow traps. Tagged `#Pure`, `#ThreadSafe`.
- **Duration** · [`export let of_hours = func(hours: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L157)
  Converts 60-minute hours to nanoseconds. Overflow traps. Tagged `#Pure`, `#ThreadSafe`.
- **Duration** · [`export let of_days = func(days: int64) -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L161)
  A day is 24 hours. Tagged `#Pure`, `#ThreadSafe`.
- **Duration** · [`export let parse = func(text: string) -> result.Result<Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/time/duration.nra#L170)
  An optional sign, P, an optional day count with D, then optionally T and at least one of hours (H), minutes (M), and seconds (S) in that order; seconds may have a fraction of one through nine digits. At least one component is required, and letters are uppercase. The to_string form always parses. Errors: Empty; InvalidCharacter or UnexpectedEnd at the first byte that does not fit; OutOfRange at the first digit of the component that leaves int64. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.7 Conformance summary

These are the intended source-module contracts. Compiler support for the operations does not imply that a collection module or adapter is already packaged.

| Type | Equality and hash | Natural order | Iterable | Interfaces (G4) |
| --- | --- | --- | --- | --- |
| Integers, floats, `bool`, fieldless enums, `string`, tuples | Built in (G3a) | Companion `compare` functions | No (`Strings.scalars` gives a list) | |
| Array, list | Built in, structural (G3a) | No | Built-in for-each | |
| `Optional<T>`, `Result<T>`, `Error`, `JsonValue` | Built in when each payload supports value members | No | No | |
| `ArrayList`, `LinkedList` | As lists | No | Yes | `MutableList`; `LinkedList` also `Deque` |
| `ArrayDeque`, `PriorityQueue` | Identity | No | Yes | `Deque` or `Queue` |
| `HashSet`, `TreeSet`, `BitSet` | As sets | No | Yes | `MutableSet`; `TreeSet` also `SortedSet` |
| `HashMap`, `TreeMap`, `OrdinalMap` | As maps | No | Yes, of `[K, V]` | `MutableMap`; `TreeMap` also `SortedMap` |
| `BigInteger`, `BigDecimal`, `Duration`, `Instant`, the calendar types, `Path`, `Uuid` | Yes, by value | `compare_to` member | No | `Comparable` |
| `RegexFlags`, `RegexMatch`, `Regex` | By value | No | No | |
| `StringBuilder`, `ByteBuffer` | Current byte sequence | No | No | |
| `Sequence`, `Random`, `Logger`, stream adapters, files, sockets | Identity | No | `Sequence` once | Streams: [section 11.1](Stdlib-Design#111-stdio) |

### 11.4 `Std.Net`

The contract tree, the TCP stream and listener, and the packages of 0.3.0. The 0.3.0 part is proposed: autonomous agents implemented it as [the networking design](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) records, and it awaits the owner's confirmation. A member that needs a host service this runtime lacks traps with `unsupported: <Module>.<member> is not implemented in Neura 0.3.0 (<reason>)` and is marked `@stub` in its source; the design note lists each one with the service it needs. Addresses, views and messages are heap objects under the tracing collector, so the specification's "zero-copy" and "zero-allocation" are not provided: an `IoSlice` is a view over one byte list, copied only when asked. Clocks and random sources are injected, so retries, caches, masks and identifiers repeat for the same inputs.

**TCP.** A call waits for its result; in a program with tasks the waiting task parks and the others run, and cancelling its scope ends the wait with kind `Cancelled` ([Concurrency](Concurrency#104-host-io)). Needs P2 and T1.

- `TcpStream`: the module functions `connect(host: string, port: int, timeout: Duration) → Result<TcpStream>` and `with_connection<T>(host, port, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). Members `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), the field `read_timeout: Duration` (zero, the initial value, for none; an expired timeout fails with kind `TimedOut`; assigning a negative duration traps with `invalid argument: negative timeout`), and `close()`. A write that is cancelled while it waits fails with kind `Cancelled` and the message `stream write cancelled after N bytes`, where N is the number of bytes already sent.
- `TcpListener`: the module function `bind(host: string, port: int) → Result<TcpListener>`. Members `accept() → Result<TcpStream>`, the read-only field `local_port: int`, and `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 traps.
- `Std.Net.Http.Fetch` (0.3.0) sends one HTTP request through the host's `neura.http.fetch` service: `request(method, url)` builds a `Request` (no headers or body, a 30-second timeout, same-origin credentials) whose `with_header`, `with_body`, `with_text`, `with_timeout` and `with_credentials` return changed copies; `send()` returns `Result<Response>`, and `get` and `post_text` are shorthands. Any status is `Ok`; `Err` means no response, and `failure_of` classifies it as a `FetchFailure` (`TooLarge`, `Busy`, `OutcomeUnknown`, and since 0.3.1 `RedirectDenied` and `NotProcessed`, are custom errors, the rest I/O kinds). The native host supports absolute HTTP(S) URLs under an explicit `http_client` grant. It never follows a redirect from HTTPS to HTTP or one that would resend the body to another origin (`RedirectDenied`), and sends only `Accept`, `Accept-Language` and `Content-Language` of the caller's fields to another origin. A grant limited to listed origins (`--allow-http-client=origin,...`) refuses any other origin, at every redirect and after name resolution, with `CapabilityDenied` whose path is the refused origin; without a list the grant reaches every origin. `NotProcessed` means an HTTP/2 peer stated that it did not process the request. [Native HTTP client services](NVM#http-client-services)
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

**Contracts.** One interface per module at the root of `Std.Net`:

- `Endpoint<Addr>`: `address() → Addr`. `Listener<Addr, Conn>` is an endpoint and `Closeable` with `accept() → Result<Conn>`; `Connector<Addr, Conn>` is an endpoint with `connect() → Result<Conn>`.
- `Transport<In, Out>` is `Closeable` with `receive() → Result<Optional<In>>`, `None` once the peer has finished, and `send(Out) → Result<Unit>`, all or an error. `FramedTransport<Frame>` is `Transport<Frame, Frame>`. `MultiplexedTransport<StreamId, Frame>` adds `open_stream(bidirectional)`, `accept_stream`, `send_on`, `receive_on` and `finish_stream`.
- `Service<Req, Resp>`: `call(Req) → Result<Resp>`; `Service.of(func)` wraps a function. `Filter<Req, Resp, In, Out>`: `apply(Req, next: Service<In, Out>) → Result<Resp>`; `SimpleFilter<Req, Resp>` keeps the types. A filter is not a service: `Filter.bind(filter, service)` gives one, `Filter.compose(outer, inner)` nests two filters, and a `FilterChain<Req, Resp>` runs its filters in the order added around the service given to `then`.

**Packages.**

- `Std.Net.Core`: `Ipv4Addr` and `Ipv6Addr` are immutable addresses with strict parsers (dotted decimal; the RFC 4291 forms with an optional `%zone`, which takes part in equality), canonical text (RFC 5952) and range predicates; `IpAddr` is an enum over the two, a mapped `::ffff:a.b.c.d` stays IPv6, and `SocketAddr` adds a port (`[2001:db8::1]:443`). `UnixAddr` holds a path or a Linux abstract name (`@name`), at most 107 bytes. `CidrBlock` tests membership, and `PrefixTrie<V>` gives the longest-prefix match. `IoSlice` and `IoSliceList` gather byte views and `write_gather` writes them through a writer that may take fewer bytes than offered; `ByteRing` is a fixed-capacity ring. `Uri` parses, resolves, normalizes and builds RFC 3986 references, `Urn` parses RFC 8141 names, `Query` reads query strings as pairs or an `a[b]` tree, `PercentEncoding` encodes components and form data, `Punycode` implements RFC 3492, and `Idna.map_uts46` uses Unicode 17.0.0 for nontransitional mapping and NFC. Call it before converting a name to A-labels. Full UTS 46 validation additionally requires contextual and bidirectional checks.
- `Std.Net.Socket`: `StreamSocket<Addr>` (a byte `Transport` with addresses, half-close and a read timeout), `DatagramSocket<Addr>` and `SeqPacketSocket<Addr>`. `TcpSocket`, `TcpServer` (a `Listener`) and `TcpConnector` (a `Connector`) run over the TCP services; an accepted socket's peer address is not reported. `SocketOptions` builds and checks `SO_REUSEADDR`, `SO_REUSEPORT`, `TCP_NODELAY`, `TCP_FASTOPEN`, `TCP_USER_TIMEOUT`, keepalive timers, buffer sizes, broadcast and Multipath TCP per socket kind. `UdpSocket`, the Unix-domain `UnixStream`, `UnixListener` and `UnixDatagram` (paths and the Linux abstract namespace), applying options and `send_file` run over the host's socket services. The host services also offer raw sockets, options that precede bind, half-close, local address lookup, stream relay, zero-copy sends where available, UDP segmentation and coalesced receive, and selection of the executing program's I/O wait backend. Raw IP operations need the raw capability in addition to network access. Socket features that depend on the OS or its configuration return a typed error if unavailable.
- `Std.Net.Dns`: a stub resolver in Neura. `Name`, `Wire` and `Message` encode and decode RFC 1035 messages with compression (forward pointers and loops rejected) and the A, AAAA, NS, CNAME, PTR, MX, TXT, SOA, SRV, NAPTR and OPT records, other types as raw bytes. `HostsFile` and `ResolvConf` parse their files. `DnsCache` caches by TTL with RFC 2308 negative caching. `StubResolver` consults hosts, then the cache, then a `DnsTransport`: it expands the search list, follows CNAMEs, matches IDs and questions, falls back to TCP on truncation, retries, rotates servers and routes name suffixes to their own servers. `TcpDnsTransport`, `UdpDnsTransport` and `SystemResolver` (the operating system's resolver, which blocks every task while it waits) run over host services, and `DotTransport` over the host's TLS. `DohTransport` uses an injected `Http.Transport` or the default HTTP host transport. `DoqTransport` exchanges DNS messages over a verified OpenSSL QUIC connection with ALPN `doq`, with an optional explicit CA bundle. `Mdns` exchanges a question over link-local multicast and accepts replies from the ingress interface network for a window of at most five seconds. `SrvLookup` and `NaptrLookup` produce `SocketAddr` lists for the load balancers.
- `Std.Net.Tls`: `TlsConfig` validates client and server configurations (TLS 1.2 and 1.3, ALPN, SNI, CA file, mutual TLS). `TlsStream`, `TlsConnector` and `TlsAcceptor` are a stream, a connector and a listener over the host's TLS services, which the release archives and a build configured with `NEURA_ENABLE_SERVER_TLS` have (OpenSSL 4.1); in another build they fail with `Unsupported`. The providers support exact version ranges and mutual TLS with explicit trust and credentials. Hostname and certificate checks are on by default for clients. kTLS is an opt-in handshake setting on systems whose OpenSSL and kernel support it. `DynamicCertResolver` polls configured paths for changes and swaps certificate bundles atomically. `Acme` computes RFC 8555 challenge values and uses an HTTPS transport and an explicit signing key for its client flow. `Spiffe` parses SPIFFE IDs and checks X.509 and JWT SVIDs against explicit trust and a required trust domain. `TlsStream.peer_certificate_chain` provides the verified DER chain for a live connection. `Handshake.tls13` drives an OpenSSL engine over a caller-supplied stream. `Handshake.dtls13` drives the TLS provider’s DTLS 1.3 record engine and cookie-validating server listener over a bound UDP socket; `Handshake.dtls_peer_certificate_chain` provides the verified DER chain of such a transport. See [known DTLS limitations](https://github.com/Kayyo321/neura/blob/main/docs/lang/Neura0.3.1.md#dtls-13-limitations).
- `Std.Net.Quic`: RFC 9000 variable-length integers and stream ID rules, RFC 9221 DATAGRAM frames, the `CongestionController` interface with `NewReno`, `Cubic` and `Ledbat` (BBRv3 is a stub), a 0-RTT replay filter and session-ticket rules. `QuicConnection` (a `MultiplexedTransport<int64, uint8[list]>`) and `QuicEndpoint` (a `Listener`) have their API and no transport.
- `Std.Net.Http`: the server API of the backend design. A `Request` is immutable (`method`, `path`, `query`, `headers` as a copy, `client`, `scheme`, `request_id`), with a body read once, whole within `body_limit` (`body()`) or in chunks (`open_body()`). A `Response` is a status, a mutable `Headers` map, and bytes or a streamed producer; `text`, `binary`, `json`, `json_value`, `stream` and `envelope` build them, and `json` takes the codec's `encode` function. A `Router` (a `Service<Request, Response>`) holds `Routes` (`get`, `post`, `put`, `patch`, `delete`, `mount`, optionally bound to a host pattern) and middleware (`use`); `dispatch` never fails: no match is 404, another method's match 405 with `Allow`, HEAD uses GET without the body, and an `Err` is 500 with the envelope `{"error":{"code","message","request_id"}}`. `check` is the startup check. `require`, `bearer`, `same_origin`, `cors` and `request_log` are middleware. `Client` sends `ClientRequest`s over a `Transport`. `Headers`, `Method`, `Status`, `Message` (`HttpRequest<Body>`, `HttpResponse<Body>`), `Wire` (RFC 9112 heads and framing), `Chunked`, `Multipart`, `CacheControl`, `Cache` (RFC 9111), `Date`, `Pool` (per-host limits, idempotent retries), `Alpn` and `AltSvc` need no host. `HostBody` and `HostSink` use the assigned server worker's body services. The default client transport uses the shared Fetch binding. `H2` opens verified TLS with ALPN `h2`, sends raw frames and encodes or decodes stateless HPACK where nghttp2 is available. HPACK encoding returns `Result`. The pair-list APIs include pseudo-fields. The HTTP/3 and MASQUE wire codecs work. The three HTTP/3 connection members, the two QPACK field section functions and the four MASQUE tunnel members carry `@stub` and return `Err(Io(Unsupported))`. `AppHandler` adapts a router to `Std.App` and compiles only where `Std.App` exists.
- `Std.Net.Rpc`: gRPC in Neura. `ProtoWire` reads and writes protobuf fields; codecs (`RpcCodec<T>`: raw, JSON, mapped JSON, protobuf; FlatBuffers and Cap'n Proto are stubs) are separate from `Framing`, which handles length-prefixed and varint-delimited messages with a size limit. Compression (0.3.1) works as follows:
  - Messages are compressed per message: `gzip` is RFC 1952, and `deflate` is the zlib format, as gRPC's compression document and RFC 9110 section 8.4.1.2 define it.
  - A decompressed message is bounded by the receive limit, with RESOURCE_EXHAUSTED.
  - Malformed data, a compressed flag without an encoding, and an unknown encoding in a response all fail with INTERNAL. A server answers an unknown request encoding with UNIMPLEMENTED.
  - Requests and responses carry `grpc-accept-encoding`. `Status`, `Metadata`, `Timeout`, `Deadline` (with `DeadlineFilter`) and `GrpcWeb` follow the gRPC protocol documents. `GrpcServer` routes `/package.Service/Method` calls, and `GrpcChannel` is a `Service<RpcRequest, RpcResponse>`; every call kind runs in memory, bidirectional streams half-duplex, and the HTTP/2 and HTTP/3 transports are stubs. `Health` implements `grpc.health.v1`; `Reflection` lists services, and its file descriptors are stubs.
- `Std.Net.Realtime.Ws` implements RFC 6455: `Frame` and `FrameDecoder` (client masking, minimal lengths, control-frame rules, a size limit), `Message` reassembly with UTF-8 checks, `CloseStatus`, the opening `Handshake`, and `WebSocket`, a `FramedTransport<Message>` over any byte `Transport` that answers pings, completes the closing handshake and fails with the matching close status; `Heartbeat` supervises pings with an injected clock. `PermessageDeflate` negotiates RFC 7692 parameters and compresses messages (0.3.1), and `WebSocket` uses it when `WebSocketOptions.permessage_deflate` is set. Details:
  - A compressed message's size limit applies to its decompressed bytes, with the same status 1009 failure as an uncompressed message.
  - Each direction keeps its window between messages unless its `*_no_context_takeover` parameter was agreed.
  - Decompression always uses a 15-bit window, since the window bits only limit the peer's compressor (RFC 7692 sections 7.1.2 and 7.2.2).
  - A message that ends with a final block is accepted.

  `ExtendedConnect` builds RFC 8441 and RFC 9220 requests (opening the streams is a stub). `Std.Net.Realtime.WebTransport` implements the wire rules of draft-ietf-webtrans-http3-14; its session I/O is a stub. `Std.Net.Realtime.Sse` parses `text/event-stream` incrementally per the WHATWG rules, writes events, and reconnects with `Last-Event-ID` over an injected fetch function.
- `Std.Net.Resilience`: filters and policies over injected clocks and random sources. `CircuitBreaker` (sliding-window failure rate, Closed, Open and HalfOpen) and `CircuitBreakerFilter`; adaptive concurrency limits (`VegasLimit`, `GradientLimit`) with `ConcurrencyLimitFilter`; `TokenBucketLimiter` and `GcraLimiter` behind `RateLimiter` and a keyed `RateLimitFilter`, the GCRA limiter over a `RateStore` hook with an in-memory store; `RetryFilter` with `Backoff` and `RetryBudget`; `HedgingFilter`, which sends a backup request when the primary passes a latency percentile and needs a `SpawnableService`; and the `LoadBalancer` implementations `RoundRobin`, `PeakEwma` and `PowerOfTwoChoices`.
- `Std.Net.Mesh`: `TraceParent`, `TraceState` and `Baggage` parse and write the W3C headers, `TraceContext` extracts and injects them on any request type, and `TracePropagationFilter` continues or starts a trace around a service. `ProxyProtocol` parses and writes PROXY protocol v1 and v2 headers with their TLVs, `ProxyProtocolListener` wraps any `Listener` to consume the header, and `ProxyProtocolConnector` writes one. `ShadowFilter` mirrors a share of requests to a bounded channel without waiting, and `ConcurrentShadowFilter` runs the copy beside the primary call. `Ebpf` describes and validates socket programs and maps; loading and attaching are stubs.

In 0.3.1, the available QUIC provider serves DNS over QUIC. TCP, UDP and Unix-domain sockets, the system resolver, DNS over TCP and UDP, and WebSockets over any byte transport run over the host. TLS streams do in TLS builds. For HTTP the library provides HTTP/1.1 parsing, serialization and in-memory routing, and a server serves a router through `Std.App` (`AppHandler`). The shared native Fetch binding serves client exchanges in HTTP/1.1 and negotiates HTTP/2 in builds with HTTP/2 support. The HTTP/3 and MASQUE wire codecs run without the host.

### 12.1 `Std.Time`

The value types are `#Immutable` classes with `compare_to`, `equals`, `hash_code`, and `to_string`. Their arithmetic is checked integer arithmetic and their text is digits and punctuation joined by concatenation. Each `parse` needs T1.

**`Duration`** is a signed count of nanoseconds in one `int64` (about ±292 years).

- Module functions: `of_nanos`, `of_micros`, `of_millis`, `of_seconds`, `of_minutes`, `of_hours`, `of_days` (24 hours), each taking `int64`; the constant `Zero`; `parse(text) → Result<Duration>`. Overflow traps (reason 1), here and in every member below.
- Members: `plus`, `minus`, `multiplied_by(int64)`, `divided_by(int64)` (truncating), `negated`, `abs`, `is_zero`, `is_negative`, `to_nanos`, `to_micros`, `to_millis`, `to_seconds` (each truncating toward zero), and `to_seconds_float64`.
- `to_string` is ISO 8601: `PT`, then hours, minutes, and seconds with their unit letters, omitting zero units, with a fractional second trimmed of trailing zeros (`PT1H30M`, `PT0.5S`). Zero is `PT0S`. A negative duration is `-` followed by the text of its absolute value.

**`Instant`** is a point on the UTC time line: `int64` seconds since 1970-01-01T00:00:00Z and an `int` nanosecond adjustment from 0 through 999,999,999. Leap seconds are ignored, as in POSIX time.

- Module functions `of_epoch_seconds(seconds, nanos)`, `of_epoch_millis`, the constant `Epoch`, and `parse`. The read-only fields `epoch_seconds` and `nanos`, and the members `plus(Duration)`, `minus(Duration)`, `duration_since(other) → Duration`, `to_epoch_millis()`, `to_local(offset: ZoneOffset) → LocalDateTime`.
- `to_string` is `YYYY-MM-DDTHH:MM:SS` with a fraction of 3, 6, or 9 digits when the nanoseconds are not zero (the shortest of those that is exact), then `Z`. `parse` accepts that form and a numeric offset in place of `Z`.

**`Clock`** is an interface with `now() → Result<Instant>`, `monotonic() → Result<Duration>` (elapsed time from an arbitrary fixed origin, never decreasing), and `sleep(duration) → Result<Unit>`.

- `SystemClock` uses the clock host services (P2) and fails with `CapabilityDenied` when they are not allowed.
- `FixedClock` holds an instant set at construction; `set(instant)` and `advance(duration)` change it; `sleep` advances it and returns at once; it never fails. Library code that needs time takes a `Clock`, so tests pass a `FixedClock`.
- `Stopwatch` takes a `Clock` and offers `start`, `stop`, `reset`, `elapsed() → Result<Duration>`, and the read-only field `is_running`.

**Calendar types** use the proleptic Gregorian calendar.

- `LocalDate` stores `year` (-999,999 through 999,999), `month` (1 through 12), and `day`, read-only fields. The module function `of(year, month, day)` traps for an invalid date. `try_of(year, month, day) → LocalDate` returns `nil` for invalid components. Conversion to and from a day count since 1970-01-01 (`to_epoch_day`, `of_epoch_day`) uses the 400-year cycle of 146,097 days. Shift the year so that it starts in March, split it into the cycle and the year within the cycle, and compute the day of the cycle using floor division so that negative years work. Members include `plus_days`, `plus_months`, `plus_years`, `days_until(other) → int64`, `day_of_week() → DayOfWeek`, `day_of_year()`, `is_leap_year()`, and `length_of_month()`. Month and year changes clamp the day to the end of the target month. `DayOfWeek` runs from `Monday` through `Sunday`, with epoch day 0 on Thursday. Text uses `YYYY-MM-DD`. Years outside 0000 through 9999 have a sign and at least four digits.
- `LocalTime`: `hour()`, `minute()`, `second()`, `nano()`, computed from the nanoseconds of the day that it stores (`to_nano_of_day()`). `plus(Duration)` wraps around midnight. Text form `HH:MM:SS` with the same fraction rule as `Instant`.
- `LocalDateTime`: a `LocalDate` and a `LocalTime`, the read-only fields `date` and `time`. `plus(Duration)` carries into the date. `at_offset(offset) → Instant`. Text form: the date, `T`, the time.
- `ZoneOffset`: total seconds from -64,800 through 64,800, the read-only field `total_seconds`. The constant `Utc`, `of_hours_minutes`, text form `Z` or `±HH:MM`.

A read-only field is an exported `last` field (an `is_running` with a get-only [accessor](Types#accessors)): code outside the class reads it as `date.year` and cannot assign it. The value classes are `#Final`, and `#NoDiscard`, so a call whose result is dropped, such as `instant.plus(span);`, warns ([Tags](Tags)).

There is no time zone database, no named zones, and no daylight-saving rules. Conversions between instants and local times take an explicit `ZoneOffset`. The ISO forms above are the only text forms.

### 18.5 Time, randomness, and logging

- **Duration text.** `parse` accepts an optional sign, `P`, an optional day count, and `T` with hours, minutes, and seconds (seconds with up to nine fraction digits); days are read but never written.
- **Instant** normalizes the nanoseconds of `of_epoch_seconds` by floor division and writes every `int64` second count; `parse` accepts years -999,999 through 999,999.
- **ZoneOffset** holds whole minutes; hours and minutes share a sign, and `compare_to` orders by total seconds.
- **Clocks.** `Clock.sleep` returns at once for a zero or negative duration. `FixedClock.of(instant)` makes a fixed clock whose monotonic reading is the total of its advances and sleeps; `advance` traps for a negative duration. `Stopwatch.create(clock)`; `start` and `stop` return `Result<Unit>` and trap when repeated.
- **Calendar arithmetic** splits off 400-year cycles with one 64-bit division and works within a cycle in 32-bit arithmetic, which keeps images that format dates several megabytes smaller.
- **Random.** `seeded` and `from_entropy` are the only constructors; `from_entropy` fails with `InvalidData` when the host returns other than 8 bytes. **Uuid** stores two big-endian `uint64` halves and has `to_bytes`.
- **Trap details.** `invalid date`, `date out of range`, `invalid time`, `invalid zone offset`, `day of week`, `negative duration`, `bound must be positive`, `low must be below high`, and `negative count` after `invalid argument: `; `stopwatch is running` and `stopwatch is stopped` after `invalid state: `.
- **Logger.** `is_enabled(Off)` is false.

### Task safety

A function that a spawned task calls must be task-safe ([Concurrency, section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)). Every function of this module is. The constant `Zero` is an object, and a constant that is an object is module state a spawned task may not read; a task writes `of_nanos(0)`. [Task safety of the library](StandardLibrary#task-safety) lists every exception, and `neura check --task-safety` reports the answer for any function.
