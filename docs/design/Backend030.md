# Neura 0.3.0: backend

This note fixes the backend contracts before they are implemented: the server host and its request workers, `Std.Net.Http` routes and middleware, the provider ABI that every application host service follows, TLS, `Std.Data.Sql` with SQLite and PostgreSQL, migrations, and operation. It is one of the notes of [the application platform](Apps030.md), which owns the limits (`S-1` and the like), budgets and code ranges referred to here. Rows move to `docs/apps/Backend.md` and `docs/runtime/Providers.md` in the change that implements them.

The owner approved the A0 design decisions on 2026-11-17. Rows marked *proposed* await implementation and qualification in A4. Later changes to a row are made here, with the reason.

The worker wire library implements the framing, identity check and message order below. The note adds no syntax and depends, by name, on records, `using`, `?` on a `Result`, constructor shorthand and named arguments.

<!-- example: fragment: Std.App, Std.Net.Http and Std.Data.Sql are not implemented until A4 -->
```neura
let app = import:Std.App;
let http = import:Std.Net.Http;
let result = import:Std.Core.Result;
let unit = import:Std.Core.Unit;

export let serve = func(context: app.Context) -> result.Result<unit.Unit> {
    last pool = context.sql_pool("main")?;
    last repository = new SqlPartRepository(pool);
    last service = new PartService(repository);
    last router = new http.Router();
    router.use(http.request_log());
    router.mount(parts_api_routes(service));
    return context.serve(router);
};
```

`serve` is the composition root: it constructs the repository, the service and the routes by hand, with `new`, and hands the router to the host. `parts_api_routes` is the adapter `neura contracts` generated.

## Implementation status 2026-11-17

*Status:* implemented by autonomous agents on the `runtime-services` branch, awaiting owner confirmation. This section records what exists, what this branch adds, and where it departs from the rules below. The rules below stay the contract; the Status column of each table is updated with the change that implements a row.

Existing at the start of the branch (Codex's A4 lane): the bounded HTTP/1.1 request decoder and the buffered connection loop (`src/server/http_wire.cpp`, `http_connection.cpp`); plain TCP and TLS 1.2/1.3 transports over OpenSSL 3.5 (`http_tcp.cpp`, `http_tls.cpp`); the transactional listener set with `SIGHUP` reload (`http_listener.cpp`); the worker frame codec, `Hello` and frame-order checks (`worker_wire.cpp`); typed provider wait sources (`src/nvm/host.hpp`).

Added by this branch:

| Piece | Where | Status |
| --- | --- | --- |
| `neura-server-host`, the foreground host: listeners, optional TLS, the startup check, S-1 and S-3 admission with 503 and `Retry-After: 1`, S-2 ready workers, the S-8 deadline (504, worker killed), S-7 response limit (500), traps (500, logged), JSON log lines, `SIGTERM`/`SIGINT` drain within S-12, `SIGHUP` TLS reload | `src/server/server_host.cpp` | implemented |
| The request worker: the same executable with `--worker`, spawned with `posix_spawn` over a socket pair on descriptor 3; it loads and verifies the image, greets, waits for one `Assign` and its body, runs the image once, and exits | `src/server/request_worker.cpp` | implemented |
| Typed `Assign`, `Head`, `Call` and `Return` bodies and a blocking checked channel | `src/server/worker_messages.cpp` | implemented |
| `neura.http.server.next`, `respond`, `body.read`, `body.write`, `body.close`, `neura.app.config`, `neura.app.log`; the `http_server` and `app` capability groups | `src/nvm/app_provider.cpp` | implemented |
| `Std.App` (`Context`, `Request`, `Response`, `Handler`, `handler_of`, `run`) and the internal binding `Std.Net.Internal.HttpServer` | `stdlib/std/app.nra`, `stdlib/std/net/internal/http-server.nra` | implemented |
| Response header fields in the connection loop; reason phrases for the common statuses | `src/server/http_connection.cpp` | implemented |
| Error codes 16 to 22 in the provider table | `src/nvm/providers.hpp` | codes defined; the library maps them to `Other` until `IoErrorKind` gains their names |
| The admin listener (`--admin-listen`: `/livez`, `/readyz`, `/metrics` in the Prometheus text format) and `neura.app.metric` (`Context.count`, `Context.gauge`) | `src/server/server_host.cpp`, `src/nvm/app_provider.cpp`, `stdlib/std/app.nra` | implemented; `neura.app.shutdown.wait` is not, by approved decision 8 |
| Streamed request bodies and streamed responses with backpressure: the request is assigned at the end of its head and its body streams to the worker; a response sent with `respond(..., complete: false)` streams back with chunked transfer coding, one chunk per write; `Context.serve_streamed`, `BodyReader`, `stream`, `BodyWriter` | `src/server/http_connection.cpp`, `server_host.cpp`, `request_worker.cpp`, `stdlib/std/app.nra` | implemented; the response is forwarded once the request body has been received |
| S-8 from the end of the request head, for the connection and the worker alike | `src/server/http_connection.cpp`, `server_host.cpp` | implemented |
| The worker step limit (`--max-steps`) and the worker tier (`--worker-tier`, `--worker-jit`) | `src/server/server_host.cpp`, `request_worker.cpp` | implemented; one value for every request (no per-route limit is defined) |
| A ready worker that has ended is discarded and the request given to another, once | `src/server/server_host.cpp` | implemented |
| `neura serve`, the closest form of `neura run <bundle>` | `src/cli/serve.cpp` | implemented: builds a source program and runs the host in the foreground; bundles wait for the project build |
| SQL, migrations, `neura run <bundle>`, `neura dev`, `trusted_proxies`, authentication helpers | | not started on this branch (SQL is the `sql-providers` branch) |

Departures, each to be confirmed by the owner:

1. **Entry.** Backend030 has `Std.App` supply the image entry and call an exported `serve`. A library module cannot do that today: an inherited `main` that calls a requirement of the derived module is rejected (`E0899`), and without the project build there is no target `entry` to find `serve` by. Until `neura build --project` exists, the entry module is an ordinary `Std.Program.Entry` whose `main` is `app.run(serve)`.
2. **No `?` and no `using`** on this branch's language baseline: `Std.App` and the examples use `match`.
3. **Header and log fields** cross the host boundary as one flat `string[list]` of alternating names and values, because a host call cannot carry a list of tuples (`E0801`). `Std.App` presents them as `[string, string][list]`.
4. **Configuration** reaches the worker through `Call`/`Return` frames; the worker never holds the host's configuration. Logs are written by the worker to the inherited standard error as JSON lines with the request identifier. No other `Call` operation exists yet.
5. **`Cancel`** is not sent: at the deadline the host kills the worker with `SIGKILL` and answers 504 (or closes the connection inside a streamed response); no grace period applies to a request past S-8. A worker whose channel ends without `Done` gets the grace period S-12 (5 s) to exit before it is killed.
6. **The handler interface** the `Std.Net.Http` router implements is `Std.App.Handler` (below), not `http.Router` itself, so that `Std.App` does not depend on `Std.Net.Http`.
7. **File paths outside the file group.** A Unix-domain socket path and the CA, certificate and key files of the TLS services are opened by the network services and are not checked against `--allow-fs` prefixes; `neura.net.tls.accept` requires the file group as a whole. A program with the network group can therefore reach any Unix socket the process can.
8. **Capabilities.** A worker always has the console, clock and entropy groups and the two application groups; files, network and environment only with `--allow-fs[=prefixes]`, `--allow-net` and `--allow-env` on the host. Native code is always denied.
9. **Retrying a dead ready worker.** "Nothing is replayed" stays the rule. The one exception is a worker that provably ran nothing: a ready worker whose channel is readable when it is taken (it sends nothing after its greeting, so it has ended), or whose channel refuses the `Assign` itself. The request goes to another worker, once. A worker that took its `Assign` is never replaced.
10. **S-7 scope.** S-7 bounds a *buffered* response: a complete body given to `respond`. A streamed body (`respond` with `complete` false, then `body.write`) is forwarded chunk by chunk, so S-7 does not count it; S-8 and backpressure bound it.
11. **Order of the two bodies.** The host sends the request body to the worker as it arrives and starts forwarding the response once the request body has ended. A worker that answers first keeps the request body that arrives while it sends (its channel reads while it waits to write), so neither side blocks the other; what it leaves unread is read and discarded before it exits.
12. **The admin listener is opt-in** (`--admin-listen`); it has no default address, and answers outside worker admission.

### Host services for the standard library

The runtime side of the `Std.Net` and `Std.Crypto` stubs. Every service is in [NVM](../runtime/NVM.md#host-services), has a capability group (denied answers 14) and is recorded and replayed like every host request. `Std.Net.Internal.Sockets` binds them for the public library.

| Service | Status | Tests |
| --- | --- | --- |
| OS entropy, `neura.entropy.fill` | implemented: `getrandom`, `getentropy`, `BCryptGenRandom`; fails with 100 instead of a weaker fallback | `backend-host` |
| UDP, `neura.net.udp.*` | implemented; numeric addresses only, no overlap | `backend-host` |
| Unix-domain streams and datagrams, `neura.net.unix.*`, with the Linux abstract namespace | implemented; not on Windows | `backend-host` |
| Socket options, `neura.net.option.*`: `SO_REUSEADDR`, `SO_REUSEPORT`, `TCP_NODELAY`, `TCP_FASTOPEN`, `TCP_USER_TIMEOUT`, keepalive and its timers, buffers, broadcast | implemented; an option the platform lacks answers 13 | `backend-host` |
| TLS client streams with verification, SNI and ALPN, and server streams outside the host, `neura.net.tls.*` | implemented in builds with `NEURA_ENABLE_SERVER_TLS`; 13 otherwise | `backend-host` (TLS builds) |
| `sendfile`, `neura.net.send_file` | implemented: `sendfile(2)` on Linux, a copy elsewhere | `backend-host` |
| `splice` with pipe handles, `neura.net.pipe` and `neura.net.splice` | implemented on Linux (`splice(2)`, the bytes never reach the NVM); 13 elsewhere | `backend-host` |
| System name resolution, `neura.net.resolve` | implemented with `getaddrinfo`; blocks every task while it waits | `backend-host` |
| HTTP/2 over TLS through ALPN, and h2c | not provided: approved decision 9 excludes HTTP/2 from 0.3.0 and allows no flag; see "HTTP/2 and decision 9" below | |
| `MSG_ZEROCOPY` | not provided, after measurement: see "MSG_ZEROCOPY" below | |
| UDP GSO and GRO: options 13 `UDP_SEGMENT` and 14 `UDP_GRO`, `neura.net.udp.receive_segments` with the segment size | implemented on Linux; 13 elsewhere | `backend-host` (loopback: a 2,500-byte send arrives as 1,000, 1,000 and 500; with GRO, coalesced with segment size 1,000) |
| MPTCP: `neura.net.mptcp.listen`, `neura.net.mptcp.connect`, option 15 `SO_PROTOCOL` | implemented on Linux 5.6 and later; 13 when the kernel has none or `net.mptcp.enabled` is 0 | `backend-host` (262 read back on this kernel) |
| `epoll` and `io_uring` driver backends, `NEURA_IO_BACKEND` | implemented on Linux, feature-detected with fallback to `poll(2)`, which stays the default; completion order is the request sequence under every backend | `nvm-wait-source-epoll`, `nvm-wait-source-io-uring`; `concurrency-io` and `backend-concurrency` run under each |
| `kqueue` driver backend | not provided: no BSD or macOS machine qualifies it in this release | |
| kTLS | not provided on this machine, see "kTLS" below | |
| Fork and VM-generation detection, `neura.process.generation` | implemented for `fork` (a change of process ID) and, on Linux with a vmgenid device, for a VM snapshot restore or clone (the `NEW_VMGENID=1` uevent) | `backend-host` for fork and the uevent parser; no vmgenid device on this machine, so a restore was not observed |
| `RDSEED` and `RNDR`, `neura.entropy.hardware` | implemented on x86-64 and AArch64 Linux; 13 elsewhere or without the instruction | `backend-host` |
| `mlock`, core-dump exclusion and zeroization: `neura.memory.lock.create`, `write`, `read`, `equal`, `release`, bound by `Std.Crypto.Internal.LockedMemory` | implemented on POSIX for a host buffer addressed by handle, outside the NVM heap, which the collector still moves and copies: bytes passed in or read out are ordinary heap copies. A recorded run cannot create a buffer. 13 on Windows | `backend-host` (the kernel's `Locked`, `dd` and `wf` flags of the mapping read from `/proc/self/smaps`) |

### kTLS

Kernel TLS moves record encryption into the kernel after OpenSSL's handshake (`SSL_OP_ENABLE_KTLS`, then `setsockopt(TCP_ULP, "tls")` and the key material). It is not provided, for two reasons found on this machine (Linux 7.1, x86-64):

1. The pinned OpenSSL 3.5.9 of the server component is configured `no-ktls` (`OPENSSL_NO_KTLS` in its `configuration.h`), so OpenSSL cannot hand a connection to the kernel. Changing that is a change of the locked component's build configuration.
2. The kernel's `tls` module is present (`tls.ko.xz`) but not loaded, and only a process with `CAP_NET_ADMIN` makes the kernel load it on `TCP_ULP`. The server host runs unprivileged, so on a default system the offload is unavailable whatever OpenSSL does.

What providing it needs: rebuild the pinned OpenSSL with `enable-ktls` and record it in the lock; set `SSL_OP_ENABLE_KTLS` in the host's and the providers' contexts; detect the offload per connection with `BIO_get_ktls_send` and `BIO_get_ktls_recv` and report it (a metric); qualify on a machine where `tls` is loaded, comparing throughput and checking `SIGHUP` reload, `close_notify` and renegotiation-free TLS 1.3 key updates, which kTLS handles differently.

### MSG_ZEROCOPY

Measured on this machine with a standalone loopback benchmark (2 GiB per run, three runs per size, while other builds ran, so indicative): `send` against `send(MSG_ZEROCOPY)` of a buffer the provider has just copied from the host value, as `neura.net.write` has it, keeping a ring of 64 copies alive until the kernel reports their completion. Results are in the table below. On loopback the kernel reports every zerocopy send as copied (`SO_EE_CODE_ZEROCOPY_COPIED`), so the flag only adds the completion bookkeeping; across a real network interface the saving is one copy per send, against a host value the provider has already copied once at the boundary, and the Linux documentation itself places the break-even near 10 KB per send. Without a measurable benefit here, and with a provider that would have to keep every sent buffer until its notification arrives, the service is not provided.

| Bytes per send | `send`, MiB/s (three runs) | `send(MSG_ZEROCOPY)`, MiB/s | Zerocopy sends the kernel copied anyway |
| --- | --- | --- | --- |
| 4,096 | 7,769; 5,979; 8,822 | 2,602; 3,767; 4,527 | all 524,288 per run, less a few in flight |
| 65,536 | 10,578; 10,738; 10,963 | 8,302; 10,007; 8,693 | all 32,768 per run, less a few in flight |
| 1,048,576 | 9,414; 9,424; 9,427 | 7,495; 7,462; 6,612 | all 2,048 per run, less one in flight |

### HTTP/2 and decision 9

Approved decision 9 reads "No WebSocket, HTTP/2 or connection upgrade in 0.3.0". It names no configuration under which HTTP/2 may be offered, so this branch does not offer it, not even behind a build option or a host flag: an option that turns it on would ship HTTP/2 in 0.3.0. The TLS listener's ALPN callback selects `http/1.1` only; a client that offers only `h2` gets no ALPN answer and either speaks HTTP/1.1 or gives up. `curl --http2` against the host negotiates HTTP/1.1, which `server-host` checks. Operators who need HTTP/2 or HTTP/3 terminate it in a reverse proxy, as the HTTP table says.

What changing it needs, in order:

1. The owner amends decision 9, here, with the scope: HTTP/2 over TLS (ALPN `h2`) only, or also cleartext h2c (prior knowledge, or the `Upgrade: h2c` that decision 9 also excludes); and whether it is on by default.
2. A pinned HTTP/2 framing library in [the server component lock](../../packaging/server-components.lock.toml), with its digest, signature and license, linked statically like OpenSSL. nghttp2 1.64 is what this machine's curl uses; a hand-written HPACK and frame layer is not reasonable to qualify for this release.
3. In the host: the ALPN callback selects `h2` when it is enabled; an HTTP/2 connection maps each stream to the same `Responder` the HTTP/1.1 loop calls (request head at `HEADERS`, body by `DATA`, response through a `ResponseWriter` whose chunks become `DATA` frames with flow control as backpressure). S-4 counts connections, and a new limit bounds concurrent streams per connection (`SETTINGS_MAX_CONCURRENT_STREAMS`), each stream still admitted by S-1 and S-3. Header limits S-5 apply to the decoded header list; HPACK table size is bounded.
4. Tests: an HTTP/2 wire suite with `curl --http2` and `nghttp`, interleaved streams, flow-control stalls, `RST_STREAM` cancelling a worker, and the existing host suite over both protocols.

### Handler interface for `Std.Net.Http`

`Context.serve(handler: app.Handler) -> Result<Unit>` receives the assigned request and calls the handler once.

```text
Std.App.Handler = interface ( let handle = func(request: Request) -> Result<Response>; )
Std.App.Request: method, target, path, query (strings), headers ([string, string][list], lowercase names,
                 arrival order), body (uint8[list], complete), client, scheme ("http" or "https"),
                 request_id; header(name) -> Optional<string>; body_text() -> Result<string>
Std.App.Response(status: int, headers: [string, string][list], body: uint8[list]); with_header(name, value)
Std.App.text, html, json (status, string) -> Response; empty(status) -> Response; handler_of(func) -> Handler
```

The router implements `Handler`, converts `app.Request` to `http.Request` and `http.Response` to `app.Response`. A handler `Err` is answered with 500 and `{"error":"internal"}`, and the error message is logged with the request identifier. A startup check is answered with 204 without calling the handler. The host owns `Content-Length`, `Transfer-Encoding` and `Connection`, ignores those fields in a response, and answers 500 for a field name that is not a token or a value with a control character. `HEAD` reaches the handler as `HEAD`; the host sends the head of the handler's response without its body.

### Message bodies

All integers are little-endian; a *text* is a `uint32` byte count and the bytes.

| Frame | Body |
| --- | --- |
| `Assign` | `uint32` kind (1 request, 2 startup check), text request identifier, `int64` deadline (Unix nanoseconds), text peer, text scheme, text method, text target, `uint32` field count and that many (text name, text value), `int64` body length, `uint32` lease count (0) |
| `Head` | `uint32` status, `uint32` field count and that many (text name, text value), `int64` body length: the exact length of a complete body, or -1 for a streamed body |
| `Call` | `uint32` call identifier, `uint32` operation, and its arguments: operation 1, configuration: text key; operation 2, metric: `uint32` kind (1 counter, 2 gauge), `int64` value, text name |
| `Return` | `uint32` call identifier, `uint32` error code (0 for success); for a configuration, `uint8` found and text value |

The host sends `Assign` at the end of the request head, then the request body as `BodyChunk` frames of at most 64 KiB as the client sends it, and `BodyEnd`. The worker runs the image as soon as it has the `Assign`; `body.read` waits for the next chunk. The worker sends `Head`, its body chunks, `BodyEnd` and `Done` as the image responds. Both sides check every frame with `ChannelSequence`.

## Layers

| Rule | Status |
| --- | --- |
| A backend is written as route, service, repository, provider. A route decodes and authorizes; a service decides; a repository runs SQL; a provider is the host. Each is constructed explicitly and passed to its user's constructor. | proposed |
| There is no container, no scan for annotations, no reflection and no ambient registry. Interfaces make repositories and services replaceable in tests; a fake provider replaces the host. | proposed |
| A server target's entry module exports `serve: func(app.Context) -> Result<Unit>`. `Std.App` supplies the image's entry point, which builds the context, calls `serve`, and ends the execution. | proposed |
| Expected failures are values: a `Result`, or a variant of a contract's reply. A trap is a defect and ends the request. | proposed |

## The server host and request workers

`neura-server-host` is a native program of the server target component. It owns the listeners, TLS, the HTTP wire, the provider pools and the workers. It runs no application code.

| Rule | Status |
| --- | --- |
| Each request is executed by one worker: an operating-system process that loads the server image into a fresh NVM, runs the whole program from its startup, handles that one request, and exits, whether it answers, fails or traps. | implemented (`neura-server-host`) |
| `context.serve(router)` takes the one request the host assigned, dispatches it, sends the response and returns. The program is written without a loop and holds nothing between requests. Data that must outlive a request is in the database. | implemented: `Context.serve` takes an `app.Handler`, which the router implements |
| The host keeps `S-2` ready workers that have loaded and verified the image and bound its services and have executed nothing: no global initializer, no `static scope`, no entry. A request is given to a ready worker, or to a new one, up to `S-1`; beyond that it waits, up to `S-3`. | implemented |
| A worker is never reused. A reuse would need a proved reset of the NVM, which 0.3.0 does not have; startup cost is measured (budget B-S1), not hidden. | implemented; B-S1 not yet measured |
| A worker is the runner (`src/runner/runner.cpp`) with a host registry whose services forward to the host over one inherited channel. It has no listener, no database connection and no file or network capability of its own beyond what the manifest grants the target. | implemented: `neura-server-host --worker` over a socket pair |
| A trap ends that worker and every task in it. The host answers 500 with the framework envelope if no byte of the response was sent, and otherwise closes the connection. It revokes the worker's leases, logs the request identifier and the trap, and starts a ready worker for later requests. Other requests are unaffected. | implemented; no leases exist yet |
| Nothing is replayed: not a request whose worker died, not a statement, not a commit. | implemented; a ready worker that ended before it took its assignment is replaced (departure 9) |
| If the host itself fails, the application ends. Neither it nor `neurad` restarts it; a deployment's supervisor may, by its own policy. | implemented |
| The host runs in the foreground: `neura run dist/server` or `neura-server-host dist/server`. Under `neura run --runtime=required` the whole tree is one application of the service. | partly: `neura-server-host <image.nvr>` and `neura serve <file.nra or image>`; bundles and `neura run dist/server` wait for the project build |
| Before it accepts, the host runs one worker with a *startup check* request. `serve` runs as usual, the router answers the check without calling a route, and migrations and configuration are verified. Readiness is true only after it succeeds. | implemented, without migrations |

### Request-worker protocol

One channel per worker: a socket pair on POSIX. Frames are a 4-byte little-endian length, a 1-byte kind, and a body; no frame exceeds `C-2` except body chunks, which are at most 64 KiB.

| Kind | Version 1 byte | Direction | Body |
| --- | --- | --- | --- |
| `Hello` | `0x01` | both | Protocol version (1), sender role, image digest, provider versions |
| `Assign` | `0x02` | host to worker | Request identifier, deadline, peer address, method, target, headers, body length, the leases granted |
| `BodyChunk` | `0x03` | both | Bytes of a request or response body, with backpressure by credit |
| `BodyEnd` | `0x04` | both | Empty. Marks the end of a request or response body |
| `Call` | `0x05` | worker to host | A host service request by import name and version |
| `Return` | `0x06` | host to worker | Completion of a host service request |
| `Cancel` | `0x07` | host to worker | The request's deadline passed, the client left, or the host is draining |
| `Head` | `0x08` | worker to host | Status and headers of the response |
| `Done` | `0x09` | worker to host | Empty. Marks completion of the response |

The version 1 length counts the kind byte and the body. It excludes the four-byte length field and is at least one. `C-2` covers the full wire frame, including those four bytes. For `BodyChunk`, the limit is 65,536 body bytes regardless of `C-2`. Other kind bytes are reserved.

Each direction starts with `Hello`. Its 39-byte fixed prefix contains a little-endian `uint32` protocol version, the sender role byte (`01` host, `02` worker), the 32 raw bytes of the image SHA-256, and a little-endian `uint16` provider count (0 to 256). Each provider entry contains a name length byte (1 to 255), that many ASCII bytes from `[A-Za-z0-9_.-]`, and a little-endian `uint32` interface version. Names are unique and appear in increasing byte order. The count covers the exact body with no trailing data. The Hello body uses at most 66,599 bytes and its frame must also fit `C-2`.

The following host frame uses the SHA-256 of the ASCII bytes `abc` and the provider map `app = 1`, `sql = 0x01020304`. The worker frame for the same identity has `02` at byte offset 9.

```text
38 00 00 00 01 01 00 00 00 01
ba 78 16 bf 8f 01 cf ea 41 41 40 de 5d ae 22 23
b0 03 61 a3 96 17 7a 9c b4 10 ff 61 f2 00 15 ad
02 00 03 61 70 70 01 00 00 00 03 73 71 6c 04 03 02 01
```

Each endpoint checks both complete greetings against the expected image and provider map from its verified configuration. The role must match the direction of the frame. A malformed frame, unsupported protocol version, wrong sender role, image mismatch or provider mismatch rejects the channel before `Assign` can be accepted. An endpoint observes inbound frames after decoder completion and checks outbound frames before submitting them to its ordered transport. The first failure rejects the channel and is retained for reporting. EOF is clean at the framing boundary between frames. EOF inside the header or body is a truncation and rejects the channel.

After both greetings, the host sends at most one `Assign`. All other request and response messages require that assignment. `BodyChunk` may repeat in either direction until that direction sends `BodyEnd`. The worker sends one `Head` before its response chunks or response `BodyEnd`, including an empty response. Its `Done` follows that `BodyEnd` and closes the response stream. The host can still finish sending the request body because the response may finish early. The host may send one `Cancel` after assignment, including when it races with `Done`. `Cancel` leaves the response outcome to the worker and transport.

Calls may overlap. Each `Return` requires an outstanding `Call`, and call and return traffic may continue after `BodyEnd` or `Done` for request cleanup. The sequence checks the outstanding count. Call identifiers, payload validation beyond `Hello` and the empty control forms, body credit, process termination, and request outcome handling belong to the worker and host integration.

## HTTP

| Rule | Status |
| --- | --- |
| The host speaks HTTP/1.1 with keep-alive and chunked bodies, over TCP and over TLS. HTTP/2 and HTTP/3 are terminated by a reverse proxy the operator runs. There is no upgrade and no WebSocket in 0.3.0 (501). | implemented for HTTP/1.1 over TCP and TLS |
| The host rejects before any worker: a head over `S-5` (431), more than 100 header fields (431), a body over `S-6` (413), both `Content-Length` and `Transfer-Encoding` (400), a transfer coding other than `chunked` (501), a folded header line, a missing or repeated `Host` (400), and a method it does not know (501). A head that takes more than 10 s is closed. | implemented (A4 decoder) and tested through the host |
| Requests on one connection are served one at a time, in order. | implemented |
| `http.Request` is immutable: `method`, `path`, `query`, `headers`, `client`, `scheme`, `request_id`, and the body, which is read whole within `S-6` (`body()`) or in chunks under a `using` owner (`open_body()`). | proposed for `Std.Net.Http`; the host side and `Std.App` (`serve_streamed`, `BodyReader`) are implemented |
| `http.Response` is a status, headers and a body: bytes within `S-7`, or a stream written under a `using` owner with backpressure. `http.json(status, value, codec)` is the usual constructor. | proposed for `Std.Net.Http`; the host side and `Std.App` (`stream`, `BodyWriter`) are implemented |
| `http.Router`: `get`, `post`, `put`, `patch`, `delete` each take a path pattern (`/parts/{id}`; segments only) and a handler `func(http.Request, http.Params) -> Result<http.Response>`. `mount` adds a generated adapter's routes. Two routes with one method and equal patterns are an error of the startup check. | proposed |
| Matching is exact on method and segment count; a literal segment beats a parameter. No match is 404; a path that matches under another method is 405 with `Allow`. `HEAD` is answered from the `GET` route without its body. | proposed |
| Middleware is `func(http.Request, http.Next) -> Result<http.Response>`. `router.use` middlewares run in the order they were added, then those of the route, then the handler; responses return through them in reverse. | proposed |
| A handler's `Err`, and a middleware's, becomes 500 with the framework envelope and no detail; the error is logged with the request identifier. A handler never sees another request's data. | implemented in `Std.App.Context.serve` |
| The client's deadline and disconnection cancel the request's scope. A handler that ignores cancellation is killed `S-12` grace seconds after `S-8`. | proposed |
| `request.client` and `request.scheme` come from the connection. `Forwarded` and `X-Forwarded-*` are used only when the peer's address is in the manifest's `trusted_proxies`, which is empty by default. | proposed |
| Authentication is `http.Authenticator`: `authenticate(http.Request) -> Result<Optional<http.Principal>>`, used by `http.require(authenticator)`. The library ships two: bearer tokens checked by a function the application supplies, and server-side sessions (`http.SqlSessions`: a 256-bit random identifier from the entropy service, stored in SQL, sent as a `Secure`, `HttpOnly`, `SameSite=Lax` cookie). It defines no cipher, signature or token format. | proposed |
| A route that authenticates by cookie and has an unsafe method requires a same-origin `Origin` or `Sec-Fetch-Site` header (`http.same_origin()`); generated adapters add it. Cross-origin access is `http.cors(policy)`, with an explicit origin list. | proposed |
| The principal comes from the authenticator, never from a request field. A generated adapter passes it to the implementation only through the context. | proposed |

## Provider ABI

A provider is trusted host code behind host services. The ABI is today's `nvm::HostService` (`src/nvm/host.hpp`): a name, a version, typed parameters, a typed success and a typed error.

| Rule | Status |
| --- | --- |
| Arguments and results are copied values (`HostValue`). A provider keeps no pointer into the execution after `invoke` or `begin` returns, and never calls into Neura: it uses no `CallbackContext`. | proposed |
| A provider call that can wait is *overlapping* and *cancellable*: the task parks, other tasks run, and cancelling its scope completes it with `cancelled`. | proposed |
| `PendingOperation` keeps `attempt`, `expire` and `deadline`. `WaitSource` has four forms: none (try at each check), POSIX descriptor and events, a level-signaled Windows handle, and a signal token from the embedding loop. `PendingRequests` waits with `poll` or `WaitForMultipleObjects` and offers a nonblocking poll to embedders. See [NVM](../runtime/NVM.md#host-services) for the signal and deadline rules. This is the only change to the interface. | implemented in the wait queue |
| Completions are applied in the order they become ready, and by request sequence among those ready at one check, as the driver does today. | proposed |
| Every request and completion is in the trace. Under replay no provider is constructed and none of its code runs. | implemented: `--record-dir`, replayed by `neura run --replay` |
| Error codes extend the existing table ([Stdlib](../lang/Stdlib.md#45-host-services)): 16 `TooLarge`, 17 `Busy`, 18 `OutcomeUnknown`, 19 `PoolExhausted`, 20 `VersionMismatch`, 21 `ConstraintViolation`, 22 `Serialization`. The library maps them to new `IoErrorKind` values of the same names. | codes defined; the library maps them to `Other` |
| Capability groups are added to `nvm::Capabilities`, all denied by default: `ui`, `http_client`, `bridge`, `http_server`, `sql`, `app`, `desktop`. Which target kind may list which is the table of [the overview](Apps030.md#capability-groups). A target's manifest lists the groups and their scope (origins, pool names, operations); the host grants exactly those. A denied service binds and answers 14, as today. | partly: `http_server` and `app` |
| Handles (connections, statements, transactions, bodies) are numbers valid for one execution, checked against a generation; a forged or stale one is `invalid_input`. | proposed |
| `import:c` and dynamic native calls stay unusable together with tasks, as today. A provider is not a way around that rule. | proposed |

Every service below is version 1. Text is UTF-8 text, bytes a `uint8` sequence, a handle an `int64`, a time a count of nanoseconds as `int64`, and *pairs* a list of name and value texts. The error of each is a `uint32` code of the table above, except the SQL services, whose error is a code and a detail text (a constraint or column name, never row data).

| Service | Arguments | Success | Waits |
| --- | --- | --- | --- |
| `neura.http.server.next` | none | Kind (`uint32`: request, startup check), request identifier text, method text, target text, header pairs, body length (`int64`, -1 when chunked), peer text, scheme text, deadline time | yes |
| `neura.http.server.respond` | Status (`uint32`), header pairs, body bytes, a flag: the body is complete | none | no |
| `neura.http.server.body.read` | Maximum count (`int32`) | Bytes; empty at the end | yes |
| `neura.http.server.body.write` | Bytes | none, once the host has taken them | yes |
| `neura.http.server.body.close` | none | none | no |
| `neura.http.fetch` | As in [Components](Components030.md#http-clients) | Status, header pairs, body bytes | yes |
| `neura.sql.acquire` | Pool name text, deadline time | Connection handle | yes |
| `neura.sql.release` | Connection handle | none | no |
| `neura.sql.begin`, `commit`, `rollback` | Connection handle | none | yes |
| `neura.sql.execute` | Connection handle, SQL text, parameters (a list of kind `uint32`, bits `uint64`, bytes) | Affected rows (`int64`) | yes |
| `neura.sql.query` | As `execute`, and a row limit (`int32`) | Column name texts, rows (each a list of values in the parameter encoding), a flag: more rows exist | yes |
| `neura.sql.open_cursor` | As `execute` | Cursor handle, column name texts | yes |
| `neura.sql.fetch` | Cursor handle, maximum rows (`int32`) | Rows, a flag: more rows exist | yes |
| `neura.sql.close_cursor` | Cursor handle | none | no |
| `neura.app.config` | Key text | A found flag and the value text | no |
| `neura.app.log` | Level (`uint32`), message text, field pairs | none | no |
| `neura.app.metric` | Name text, kind (`uint32`: counter, gauge), value (`int64`) | none | no |
| `neura.app.shutdown.wait` | none | none, when the host starts to drain | yes |

A service that waits is overlapping and cancellable.

### TLS

| Rule | Status |
| --- | --- |
| The host terminates TLS with OpenSSL 3.5, linked statically into the server target component. TLS 1.2 and 1.3 with the library's default cipher list; nothing older. | implemented with the pinned 3.5.9 when built with `NEURA_ENABLE_SERVER_TLS` |
| Certificate and key are files named in the host's configuration, read at start and on `SIGHUP`. A missing or mismatched pair fails the start. | implemented (`--cert`, `--key`, `SIGHUP`) |
| An outgoing `https` fetch verifies the peer against the system trust store, or a bundle the configuration names. Verification cannot be turned off from Neura code. | proposed |
| Plain HTTP and HTTPS can listen together, each on its own address. | implemented |

The [server component lock](../../packaging/server-components.lock.toml) pins OpenSSL 3.5.9. It records the source archive digest, the upstream release-signing key and the bundled license. The server component links the static libraries. The standalone TLS tests also run with a system installation of OpenSSL 3.5.

## SQL

| Rule | Status |
| --- | --- |
| `context.sql_pool(name)` returns the `sql.Pool` the manifest declares under that name. The connections live in the host; the worker holds a lease. | proposed |
| `pool.acquire()` returns `Result<sql.Connection>`, and `pool.begin()` a `Result<sql.Transaction>`. Both are closeable and are owned by `using`. A wait beyond `S-10` is `Err`. | proposed |
| SQL is a `string`; values are parameters. `execute(sql:, parameters:)` returns the affected row count; `query_one` a row or `Err(NotFound)`; `query_page(sql:, parameters:, limit:)` a `FrozenList<sql.Row>` of at most `limit` and `S-11` rows; `open_cursor` a `sql.Cursor` owned by `using`, fetched in pages. | proposed |
| Parameters are a tuple or array of `sql.Value`: `Null`, `Bool`, `Int(int64)`, `Float(float64)`, `Text(string)`, `Bytes`. Scalars convert implicitly at the call. The provider copies them before the task parks. Placeholders are `$1`, `$2` for both databases; the SQLite provider rewrites them. | proposed |
| `sql.Row` is immutable. `row.int64("id")`, `row.text("name")`, `row.bool(...)`, `row.float64(...)`, `row.bytes(...)` and their `optional_` forms return a `Result`: a missing column, a `NULL` in a required column, or a type that does not convert exactly is `Err`. No integer is narrowed or rounded silently. | proposed |
| A transaction ends by `commit()` or by `close()`. `close()` without a commit rolls back. After a commit that returned `Ok`, `close()` releases only. | proposed |
| If the connection fails after the commit was sent and before its answer, `commit()` returns `Err(OutcomeUnknown)`. The library does not retry, does not report a rollback, and the lease's connection is discarded. | proposed |
| A serialization failure or deadlock is `Err(Serialization)`; a unique, foreign-key or check failure is `Err(ConstraintViolation)` with the constraint's name. Retrying is the application's decision. | proposed |
| Cancelling the scope cancels the running statement: `PQcancel` for PostgreSQL, `sqlite3_interrupt` for SQLite. The call returns `Err(Cancelled)` and the transaction is rolled back. | proposed |
| A cursor or transaction cannot be held across a generator's suspension, by the rule of the generator milestone. | proposed |
| When a worker exits, the host rolls back its open transactions and returns each connection to the pool after a reset (`DISCARD ALL`; for SQLite, a finalized statement set), or closes it if its state is unknown. | proposed |
| SQLite 3.54.1 is compiled in from the amalgamation, in WAL mode, with `foreign_keys` on and a busy timeout equal to the statement's deadline. Statements run on host threads, never on the listener's. | proposed |
| PostgreSQL uses libpq 18.7 in non-blocking mode against servers 16, 17 and 18, with `sslmode=verify-full` unless the configuration lowers it for loopback. Passwords come from the host's configuration only. | proposed |
| Types that convert: SQLite `INTEGER`, `REAL`, `TEXT`, `BLOB`, `NULL`; PostgreSQL `bool`, `int2`, `int4`, `int8`, `float4`, `float8`, `text`, `varchar`, `bytea`, `uuid` and `timestamptz` (as text), and `numeric` (as text). Any other column type is `Err(Unsupported)` naming it. | proposed |

### Migrations

| Rule | Status |
| --- | --- |
| A pool's migrations are files `NNNN_name.sql` in the directory the manifest names, numbered from 0001 without gaps, one or more statements each. The build copies them into the bundle with their digests; a gap, a repeat or an empty file is `E1044`. | proposed |
| The host records applied migrations in `neura_migrations(version, name, checksum, applied_at)`. Each is applied in its own transaction, in order, forward only. | proposed |
| `neura-server-host migrate dist/server` applies them and exits. With `migrate = "on_start"` the host applies them before the startup check; with `"manual"` (the default) it refuses to become ready while any is pending. | proposed |
| An applied migration whose checksum differs from the bundle's refuses the start. Nothing is repaired or re-run. | proposed |
| Concurrent hosts serialize on a PostgreSQL advisory lock, or on SQLite's write lock. | proposed |

## Operation

| Rule | Status |
| --- | --- |
| Configuration is the bundle's defaults overlaid by a file the operator names and by environment variables with the prefix the manifest declares. It is validated in full before the host listens: unknown keys, wrong types, missing secrets and unreadable files each fail the start with every problem listed. | proposed |
| A secret (a database password, a key file) exists only in the server host's configuration. `context.config` exposes the keys the manifest marks `expose`, and no secret can be marked so for a `web` target. | proposed |
| The host answers `/livez` (the process is running) and `/readyz` (the startup check passed, pools are connected, it is not draining) itself, on an admin listener bound to loopback by default. `/metrics` on the same listener gives the counters of `neura.app.metric` and the host's own in the Prometheus text format. | implemented with `--admin-listen` (opt-in), without pools |
| Logs are JSON lines on standard error: time, level, request identifier, `traceparent` if the request carried one, message, and named fields, within `S-13`. | implemented, without `traceparent` |
| On `SIGTERM`, `SIGINT`, or a console close event, the host stops accepting, answers 503 to waiting requests, cancels no worker yet, and waits up to `S-12` for workers to finish; then it cancels the rest, waits the grace period, kills what remains, reports each, and exits. `context.on_shutdown` callbacks do not exist: a request sees only its own cancellation. | partly: stops accepting, 503 for new requests, waits S-12, then kills; no cancel or grace step |
| This drain is the host's own control event. `neurad stop` keeps its meaning of external termination. | proposed |

## Lowering and artifacts

No NeuraIR, NVR or tier change. A server image is an ordinary image whose imports include the services above; it runs under `neura run --replay` with no host at all.

## Diagnostics

| Code | Title | Span | Fix |
| --- | --- | --- | --- |
| `E1044` | invalid migration set | The manifest's `migrations` value, with the file: a gap, a repeated number, a name that does not match, an empty file | Renumber or remove the file |

A server target also meets `E1024` (no `serve`), `E1027` (a capability used and not declared), `E1030` (an import the target cannot have) and `E1031` (an unknown provider), owned by [Projects](Projects030.md#diagnostics). Everything else in this note is a run-time outcome: a status, an `Err`, a refused start, or a trap.

## Edge cases

Accepted:

1. A `GET` with a query and no body; a `POST` with a chunked body of exactly `S-6` bytes.
2. Two requests on one connection, the second sent before the first is answered: served in order.
3. `HEAD /parts/7` answered from the `GET` route, with `Content-Length` and no body.
4. `/parts/new` and `/parts/{id}` both registered: the literal wins.
5. A handler that returns a streamed body and finishes writing after the client slows down: backpressure, no growth.
6. `Expect: 100-continue`: the host answers before reading the body.
7. A request with a `traceparent` header: the same value in the log and in an outgoing fetch.
8. Three middlewares added in order: the trace shows 1, 2, 3, handler, 3, 2, 1.
9. A middleware that answers without calling `next`.
10. A transaction that commits; one that returns early and rolls back on `close`; one whose body returns an `Err` through `?`.
11. `query_page` with `limit: 0`: an empty list.
12. A `NULL` read with `optional_text`: `None`.
13. An `int8` of -9,223,372,036,854,775,808 read as `int64`.
14. The same repository code against SQLite and PostgreSQL, with `$1` placeholders.
15. A second host starting while the first applies migrations: it waits, then finds none pending.
16. A configuration secret given by environment variable and absent from every bundle file.
17. A drain with no request in flight: exit at once.
18. The image run under `--replay` with recorded requests and SQL answers: the same responses, no socket, no database.

Rejected or failed:

19. A head of `S-5` plus one byte: 431, no worker started.
20. `Content-Length` and `Transfer-Encoding: chunked` together: 400.
21. A body of `S-6` plus one byte: 413, the connection closed.
22. `Upgrade: websocket`: 501.
23. No route: 404. A route under another method: 405 with `Allow`.
24. Two routes `GET /parts/{id}` and `GET /parts/{code}`: the startup check fails; the host does not become ready.
25. A handler returns `Err`: 500 with the envelope and no message of the error.
26. A handler traps while 7 other requests run: its client gets 500, the others their answers; the process count returns to its level; the trapped request is in the log once and is not run again.
27. A worker that traps after sending half a streamed body: the connection is closed; the client sees a truncated body.
28. `S-1` workers busy and `S-3` waiting: the next request gets 503 with `Retry-After`.
29. A request past `S-8`: cancelled; 504 if nothing was sent; killed after the grace period if it does not end.
30. A client that disconnects during a query: the statement is cancelled and the transaction rolled back.
31. `X-Forwarded-For` from a peer not in `trusted_proxies`: ignored.
32. A cookie-authenticated `POST` with a cross-site `Origin`: 403.
33. A request body field naming another user as owner: ignored by the generated adapter; the principal is the authenticator's.
34. `pool.acquire()` with the pool exhausted for longer than `S-10`: `Err(TimedOut)`.
35. The database connection dropped between `COMMIT` and its answer: `Err(OutcomeUnknown)`; a second attempt by the application is its own decision.
36. A unique violation: `Err(ConstraintViolation)` with the constraint name.
37. `row.int32("count")` on a value of 3,000,000,000: `Err`, not a wrapped number.
38. A column of type `jsonb`: `Err(Unsupported)`.
39. Using a transaction after `close`: `Err(InvalidInput)`.
40. A handle number invented by the program: `Err(InvalidInput)`.
41. `0001_a.sql` and `0003_c.sql` with no `0002`: `E1044` at build.
42. An applied migration edited afterwards: the host refuses to start and names it.
43. Pending migrations with `migrate = "manual"`: `/readyz` is 503 and no request is accepted.
44. A missing certificate file, or a key that does not match: the start fails before listening.
45. A server image that calls `neura.fs.open` without the `fs` capability declared: `E1027` at build; if reached through a prebuilt library, error 14 at run time.
46. The host killed with `SIGKILL`: workers notice the closed channel and exit; nothing restarts.
47. A worker whose `Hello` names another image digest: ended before `Assign`.

## Order of work

Each step is one commit with its targeted tests. None changes the image of a program that does not use the feature.

| Step | Content | Tests |
| --- | --- | --- |
| 1 | This note | Document checks |
| 2 | The wait-source change to `PendingOperation` and the driver; the new error codes and capability groups | Existing concurrency and host suites unchanged; a unit test per wait form available on the platform |
| 3 | The host: listeners, HTTP/1.1 wire, limits, TLS; a fixed in-process responder in place of workers | Wire tests of cases 1 to 3, 6, 19 to 22, 44 |
| 4 | The worker protocol, ready pool, assignment, trap handling, drain; `neura.http.server.*`; `Std.Net.Http` requests, responses, router, middleware | Cases 4, 5, 8, 9, 23 to 29, 46, 47; the isolation journey |
| 5 | `neura.sql.*` with SQLite; `Std.Data.Sql`; transactions and cursors | Cases 10 to 13, 34, 36 to 40 on a temporary database |
| 6 | PostgreSQL; cancellation; outcome unknown with a proxy that cuts the connection | Cases 14, 30, 35 against servers 16 and 18 in containers |
| 7 | Migrations and `E1044` | Cases 15, 41 to 43 |
| 8 | Configuration, secrets, health, metrics, logs, authentication helpers, trusted proxies | Cases 7, 16, 31 to 33 |
| 9 | Replay of a server image; the load, saturation, drain and sustained budgets | Case 18; budgets B-S1 to B-S6 |

## Gate and acceptance

- HTTP and HTTPS both serve the reference application's contract, with SQLite and with PostgreSQL.
- With 8 requests in flight and one trapping, the other 7 complete, the trapped one is answered or closed once, its leases are revoked, a new worker serves only later requests, startup runs in each worker, and no request is executed twice. The same holds with a commit of unknown outcome, and with the host killed.
- Every limit `S-1` to `S-13` has a test at the limit and one past it.
- Denied capabilities answer 14; a forged handle is refused; no secret appears in any bundle file or log.
- Cancellation reaches a running statement on both databases.
- The budgets B-S1 to B-S6 are met on the machine and commands [the overview](Apps030.md#budgets) names.
- A server image runs under replay without a host.

## Migration

A 0.2 server built on `TcpListener` and hand-written parsing keeps working unchanged. Moving it to this model means: routes in a router, state out of globals and into the database, and one `serve` function. A program that assumed a trap would end only one task, or that a handler could keep an object for the next request, must change, because each request now has its own execution.

## Approved design decisions

1. The isolation unit is a process per request with a full startup, and no reuse, as the stricter reading of trap isolation.
2. The host, not Neura code, parses the HTTP wire and terminates TLS; routing and validation are Neura code in the worker.
3. Database connections live in the host and are leased, so a dead worker cannot leave a transaction open.
4. Placeholders are `$n` for both databases.
5. Session authentication stores random identifiers server-side; no token format or cryptography is defined.
6. Migrations are forward-only SQL files, manual by default, and a changed checksum refuses the start.
7. Health and metrics are served by the host on a loopback admin listener.
8. There is no shutdown callback in application code: a request sees its own cancellation only.
9. No WebSocket, HTTP/2 or connection upgrade in 0.3.0.
10. `PendingOperation` gains a platform-neutral wait source; nothing else in the host interface changes.
