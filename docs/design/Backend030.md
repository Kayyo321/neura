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
| The worker zygote (`--worker-start zygote`, the default on Linux): one helper process loads and verifies the image once and forks each worker with `CLONE_PARENT`, so the worker is still the host's child; a zygote that ends is replaced up to three times, then workers start with `exec` (`--worker-start exec`) | `src/server/worker_zygote.cpp`, `server_host.cpp` | implemented on Linux (departure 17); `exec` elsewhere |
| A worker holds its S-1 slot until the host has reaped it; the reaper waits on each worker's `pidfd` (Linux 5.3 and later; elsewhere it looks every 2 ms) | `src/server/server_host.cpp` | implemented |
| Typed `Assign`, `Head`, `Call` and `Return` bodies and a blocking checked channel | `src/server/worker_messages.cpp` | implemented |
| `neura.http.server.next`, `respond`, `body.read`, `body.write`, `body.close`, `neura.app.config`, `neura.app.log`; the `http_server` and `app` capability groups | `src/nvm/app_provider.cpp` | implemented |
| `Std.App` (`Context`, `Request`, `Response`, `Handler`, `handler_of`, `run`) and the internal binding `Std.Net.Internal.HttpServer` | `stdlib/std/app.nra`, `stdlib/std/net/internal/http-server.nra` | implemented |
| Response header fields in the connection loop; reason phrases for the common statuses | `src/server/http_connection.cpp` | implemented |
| Error codes 16 to 22 in the provider table | `src/nvm/providers.hpp` | codes defined; the library maps them to `Other` until `IoErrorKind` gains their names |
| The admin listener (`--admin-listen`: `/livez`, `/readyz`, `/metrics` in the Prometheus text format) and `neura.app.metric` (`Context.count`, `Context.gauge`) | `src/server/server_host.cpp`, `src/nvm/app_provider.cpp`, `stdlib/std/app.nra` | implemented; `neura.app.shutdown.wait` is not, by approved decision 8 |
| Streamed request bodies and streamed responses with backpressure: the request is assigned at the end of its head and its body streams to the worker; a response sent with `respond(..., complete: false)` streams back with chunked transfer coding, one chunk per write; `Context.serve_streamed`, `BodyReader`, `stream`, `BodyWriter` | `src/server/http_connection.cpp`, `server_host.cpp`, `request_worker.cpp`, `stdlib/std/app.nra` | implemented; the response is forwarded once the request body has been received |
| S-8 from the end of the request head, for the connection and the worker alike | `src/server/http_connection.cpp`, `server_host.cpp` | implemented |
| The worker step limit (`--max-steps`) and the worker tier (`--worker-tier`, `--worker-jit`) | `src/server/server_host.cpp`, `request_worker.cpp` | implemented; one value for every request (no per-route limit is defined) |
| A ready worker that has ended, or a worker whose channel refuses its `Assign`, is discarded and the request given to another, once; a second refusal answers 500 | `src/server/server_host.cpp` | implemented; the refusal is tested through the hook `NEURA_SERVER_TEST_REFUSE_ASSIGN` (`test_refused_assignment`) |
| `neura serve`, the closest form of `neura run <bundle>` | `src/cli/serve.cpp` | implemented: builds a source program and runs the host in the foreground; bundles wait for the project build |
| `Std.Data.Sql` (`Pool`, `Connection`, `Transaction`, `Cursor`, `Row`, `Value`, `SqlError`, `in_transaction`) and `Context.sql_pool` | `stdlib/std/data/sql.nra`, `stdlib/std/app.nra` | implemented (`sql_pool`, steps 5 to 7 below) |
| `neura.sql.*` services and the `sql` capability group; `Call` operation 2 over the worker channel; pools, sessions and leases in the host; `--sql-pool` for `neura run` and `neura test` | `src/nvm/sql_services.cpp`, `src/server/sql_channel.cpp`, `src/server/sql_host.cpp`, `src/sql/` | implemented |
| SQLite 3.54.1 provider, compiled in from the pinned amalgamation | `src/sql/sqlite_connection.cpp` | implemented |
| PostgreSQL provider over libpq 18.7, non-blocking, servers 16, 17 and 18 | `src/sql/postgres_connection.cpp` | implemented behind `NEURA_ENABLE_POSTGRES` |
| Migrations, `neura-server-host migrate` | `src/sql/migrations.cpp` | implemented; the `E1044` check runs in the host, not in a project build |
| `neura run <bundle>`, `neura dev`, `trusted_proxies`, authentication helpers | | not started |

Departures, each to be confirmed by the owner:

1. **Entry.** Backend030 has `Std.App` supply the image entry and call an exported `serve`. A library module cannot do that today: an inherited `main` that calls a requirement of the derived module is rejected (`E0899`), and without the project build there is no target `entry` to find `serve` by. Until `neura build --project` exists, the entry module is an ordinary `Std.Program.Entry` whose `main` is `app.run(serve)`.
2. **No `?` and no `using`** on this branch's language baseline: `Std.App` and the examples use `match`.
3. **Header and log fields** cross the host boundary as one flat `string[list]` of alternating names and values, because a host call cannot carry a list of tuples (`E0801`). `Std.App` presents them as `[string, string][list]`.
4. **Configuration** reaches the worker through `Call`/`Return` frames; the worker never holds the host's configuration. Logs are written by the worker to the inherited standard error as JSON lines with the request identifier. No other `Call` operation exists yet.
5. **`Cancel`** is not sent: at the deadline the host kills the worker with `SIGKILL` and answers 504 (or closes the connection inside a streamed response); no grace period applies to a request past S-8. A worker whose channel ends without `Done` gets the grace period S-12 (5 s) to exit before it is killed.
6. **The handler interface** the `Std.Net.Http` router implements is `Std.App.Handler` (below), not `http.Router` itself, so that `Std.App` does not depend on `Std.Net.Http`.
7. **File paths outside the file group.** A Unix-domain socket path and the CA, certificate and key files of the TLS services are opened by the network services and are not checked against `--allow-fs` prefixes; `neura.net.tls.accept` requires the file group as a whole. A program with the network group can therefore reach any Unix socket the process can.
8. **Capabilities.** A worker always has the console, clock and entropy groups and the two application groups; files, network and environment only with `--allow-fs[=prefixes]`, `--allow-net` and `--allow-env` on the host, and the `sql` group when the host has pools. Native code is always denied.
9. **Retrying a dead ready worker.** "Nothing is replayed" stays the rule. The one exception is a worker that provably ran nothing: a ready worker whose channel is readable when it is taken (it sends nothing after its greeting, so it has ended), or whose channel refuses the `Assign` itself. The request goes to another worker, once. A worker that took its `Assign` is never replaced.
10. **S-7 scope.** S-7 bounds a *buffered* response: a complete body given to `respond`. A streamed body (`respond` with `complete` false, then `body.write`) is forwarded chunk by chunk, so S-7 does not count it; S-8 and backpressure bound it.
11. **Order of the two bodies.** The host sends the request body to the worker as it arrives and starts forwarding the response once the request body has ended. A worker that answers first keeps the request body that arrives while it sends (its channel reads while it waits to write), so neither side blocks the other; what it leaves unread is read and discarded before it exits.
12. **The admin listener is opt-in** (`--admin-listen`); it has no default address, and answers outside worker admission.
13. **SQL calls do not overlap.** The `neura.sql.*` services are synchronous: a statement holds its execution, and every task in it, until it ends. The request's deadline interrupts it (SQLite's progress handler, which makes the statement fail as `sqlite3_interrupt` would; `PQcancel`); a scope's cancellation and a client's disconnection do not. A true overlapping call needs a multiplexed channel in the worker.
14. **SQL values and errors.** A host call cannot carry a list of tuples, so parameters and rows cross as four parallel lists (kinds, bits, texts, blobs), and a host error is a `uint32` code only, so the detail text (a constraint or column name) is read with a further service, `neura.sql.detail`, which answers in the execution. `acquire` takes a wait in nanoseconds rather than an absolute deadline.
15. **SQL library shape.** Without `using`, implicit conversions or named arguments on this baseline, the statement methods take their parameters as a trailing variadic argument list (`execute(sql, parameters...)`), whose strings, integers, floats and Booleans become `Value`s, and a `Value` object stands for `NULL` and bytes; `in_transaction(pool, body)` stands in for a `using` block. Every SQL failure is an `Error.Custom` holding an `SqlError` with its own `ErrorKind`, so that the new kinds need no change to `IoErrorKind`. Using a closed connection, transaction or cursor is `InvalidInput`, as edge case 39 says.
16. **`E1044` in the host.** There is no project build to report `E1044`, so `neura-server-host` checks a migration directory when it starts and reports `error[E1044]: invalid migration set` with the file; the code is not in the compiler's registry yet. Pending migrations in `manual` mode refuse the start, so the host does not reach the point where `/readyz` could answer 503.
17. **Worker start from a zygote.** Approved decision 1 asks for a process per request "with a full startup, and no reuse". On Linux a worker is now forked from a zygote that read, hashed, loaded and verified the image once, instead of `exec`-ing the host and doing those steps itself. The reading taken here: what decision 1 protects is that no state of one request reaches another, and that every request runs the whole program from its startup. Both still hold: the zygote runs no Neura code (no global initializer, no `static scope`, no entry) and holds no request data, so a forked worker is in the state an `exec`-ed worker reaches after its verification, and runs the whole program for its one request. What differs: forked workers share the zygote's address-space layout (ASLR differs between the host and its workers, not between workers), and the image bytes are verified once per zygote rather than once per worker. `--worker-start exec` restores the earlier start.

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
| HTTP/2 over TLS through ALPN `h2` (on by default, `--no-http2`), and h2c with prior knowledge (`--h2c`) | implemented in builds with `NEURA_ENABLE_SERVER_HTTP2` (the pinned nghttp2 1.71.0), by the owner's amendment of decision 9; see "HTTP/2 and decision 9" below | `server-host` (`test_http2_curl`, `test_http2_streams`, `test_http2_reload`), `server-host-h2c` |
| `MSG_ZEROCOPY` | not provided, after measurement: see "MSG_ZEROCOPY" below | |
| UDP GSO and GRO: options 13 `UDP_SEGMENT` and 14 `UDP_GRO`, `neura.net.udp.receive_segments` with the segment size | implemented on Linux; 13 elsewhere | `backend-host` (loopback: a 2,500-byte send arrives as 1,000, 1,000 and 500; with GRO, coalesced with segment size 1,000) |
| MPTCP: `neura.net.mptcp.listen`, `neura.net.mptcp.connect`, option 15 `SO_PROTOCOL` | implemented on Linux 5.6 and later; 13 when the kernel has none or `net.mptcp.enabled` is 0 | `backend-host` (262 read back on this kernel) |
| `epoll` and `io_uring` driver backends, `NEURA_IO_BACKEND` | implemented on Linux, feature-detected with fallback to `poll(2)`, which stays the default; completion order is the request sequence under every backend | `nvm-wait-source-epoll`, `nvm-wait-source-io-uring`; `concurrency-io` and `backend-concurrency` run under each |
| `kqueue` driver backend | not provided: no BSD or macOS machine qualifies it in this release | |
| kTLS | not provided on this machine, see "kTLS" below | |
| Fork and VM-generation detection, `neura.process.generation` | implemented for `fork` (a change of process ID) and, on Linux with a vmgenid device, for a VM snapshot restore or clone (the `NEW_VMGENID=1` uevent) | `backend-host` for fork and the uevent parser. Unverified here: the kernel has the driver (`CONFIG_VMGENID=y`) but this machine has no vmgenid device, so no restore was observed, and the `NEW_VMGENID=1` field is taken from the kernel driver's documented behavior, not from a captured uevent |
| `RDSEED` and `RNDR`, `neura.entropy.hardware` | implemented on x86-64 and AArch64 Linux; 13 elsewhere or without the instruction | `backend-host` |
| `mlock`, core-dump exclusion and zeroization: `neura.memory.lock.create`, `write`, `read`, `equal`, `release`, bound by `Std.Crypto.Internal.LockedMemory` | implemented on Linux for a host buffer addressed by handle, outside the NVM heap, which the collector still moves and copies: bytes passed in or read out are ordinary heap copies. A recorded run cannot create a buffer. 13 elsewhere (no `MADV_DONTDUMP` and `MADV_WIPEONFORK`) | `backend-host` (the kernel's `Locked`, `dd` and `wf` flags of the mapping read from `/proc/self/smaps`) |

### Budgets measured

`scripts/bench-server.py` measured the budgets of [the overview](Apps030.md#budgets) on this machine (Linux 7.1, x86-64, 8 cores), with `tests/server/fixtures/test-server.nra` answering `GET /info` (JSON, no database), 32 keep-alive connections and 8 workers unless noted. Other agents' builds and tests ran throughout (load average 6 to 16), so every figure is indicative; none of the budgets was changed to fit. The loader's own ceiling, against a Python echo server with Nagle's algorithm off, is 13,800 to 15,400 requests per second, above every result, so the results measure the host. The table is the 2026-11-18 run with workers started from the zygote (`runtime-services` e2618d7b); the evidence is under the lane's `runtime-r3/` directory.

| Budget | Workload | Result | Met |
| --- | --- | --- | --- |
| B-S1 latency, fresh worker | 500 requests per second fixed, 60 s after 10 s | median 1.01 ms, 95th 1.14 ms, 99th 1.69 ms; 30,000 of 30,000 answered 200 | yes |
| B-S2 throughput | closed loop, 30 s after 10 s, three pairs alternating with the 686c8194 host | zygote: 4,872, 3,677 and 3,217 requests per second (mean 3,922), no 503; 686c8194: 1,617, 1,435 and 1,199 (mean 1,417). A 60 s run gave 3,950. | yes |
| B-S2 CPU per request | host and reaped workers (`utime`, `stime`, `cutime`, `cstime` of the host) over the B-S2 runs | 1.01 to 1.03 ms with the zygote; 3.74 to 3.77 ms with 686c8194 | |
| B-S4 saturation | twice the B-S2 rate (7,901 per second), 64 connections, `--max-queue 8`, 30 s | 503 within 1.75 ms at the 95th percentile (134,229 of 237,024 answered 503); peak process tree 12, of which 8 zombies holding their slots, against a limit of 12 (host, zygote, S-1, S-2) | yes |
| B-S5 drain | `SIGTERM` under 500 requests per second | exit in 0.16 s with status 0 | exit time yes; that every request in flight completes rests on `test_drain_completes_responses`, because the loader cannot tell a request in flight from one refused after the listener closed (963 such errors) |
| B-S6 sustained | half the B-S2 rate (1,975 per second) for 600 s, one request in 1,000 trapping | resident size slope 0.015% per minute after minute 2; 1,185 traps answered 500; afterwards 9 descriptors and 4 processes (the host, the zygote and 2 ready workers) | yes (the run's own check counted the zygote as a leftover; the script now allows it) |
| B-S3 latency with SQL | | not measured here: SQL is the `sql-providers` change | |

Each pair ran the same loader for the two hosts within two minutes of each other; the load average at each run is in the reports. The B-S2 loads were 11 to 16, so the absolute rates move by a third between pairs while the ratio stays between 2.6 and 3.0.

What changed since the 2026-11-17 run (B-S2 929 per second, B-S4 peak 22 processes against 12):

1. **B-S2.** Profiling the 686c8194 worker put about 2.6 ms of its 4 ms of CPU per request in startup: the exec (0.4 ms), reading the image (0.42), its SHA-256 (1.16), and loading and verifying it (0.6); the run itself took 0.95 ms, and the reaper's polling about 2 ms of wall time. Workers now fork from a zygote that did that startup once (departure 17), and the reaper waits on `pidfd`s instead of polling, which cut the CPU per request to about 1 ms.
2. **B-S4 process count.** A worker now holds its S-1 slot until the host has reaped it, so exited workers count against S-1 and the tree never exceeds S-1 + S-2 + 2. `test_process_bound` checks the bound in both start modes.

**Worker tier policy.** For this route the tier makes no measurable difference: sequential `GET /info` on one connection, 400 requests, median 1.35 ms by default (tier 2 with tier 3 on x86-64), 0.77 ms at `--worker-tier 0`, 1.08 ms at tier 1, 1.10 ms with `--worker-jit off`, 1.30 ms with `NEURA_TIER3=off`, with 95th percentiles from 2.7 to 3.9 ms. The default stays; `--worker-tier` and `--worker-jit` let an operator choose for a heavier route, which this measurement does not cover.

### kTLS

Kernel TLS moves record encryption into the kernel after OpenSSL's handshake (`SSL_OP_ENABLE_KTLS`, then `setsockopt(TCP_ULP, "tls")` and the key material). It is not provided, for two reasons found on this machine (Linux 7.1, x86-64):

1. The pinned OpenSSL 3.5.9 of the server component is configured `no-ktls` (`OPENSSL_NO_KTLS` in its `configuration.h`), so OpenSSL cannot hand a connection to the kernel. Changing that is a change of the locked component's build configuration.
2. The kernel's `tls` module is present (`tls.ko.xz`) but not loaded, and only a process with `CAP_NET_ADMIN` makes the kernel load it on `TCP_ULP`. Observed: `setsockopt(TCP_ULP, "tls")` on a connected loopback socket, from an unprivileged process, fails with `ENOENT`. The server host runs unprivileged, so on a default system the offload is unavailable whatever OpenSSL does.

What providing it needs: rebuild the pinned OpenSSL with `enable-ktls` and record it in the lock; set `SSL_OP_ENABLE_KTLS` in the host's and the providers' contexts; detect the offload per connection with `BIO_get_ktls_send` and `BIO_get_ktls_recv` and report it (a metric); qualify on a machine where `tls` is loaded, comparing throughput and checking `SIGHUP` reload, `close_notify` and renegotiation-free TLS 1.3 key updates, which kTLS handles differently.

### MSG_ZEROCOPY

Measured on this machine with a standalone loopback benchmark (2 GiB per run, three runs per size, while other builds ran, so indicative): `send` against `send(MSG_ZEROCOPY)` of a buffer the provider has just copied from the host value, as `neura.net.write` has it, keeping a ring of 64 copies alive until the kernel reports their completion. Results are in the table below. On loopback the kernel reports every zerocopy send as copied (`SO_EE_CODE_ZEROCOPY_COPIED`), so the flag only adds the completion bookkeeping; across a real network interface the saving is one copy per send, against a host value the provider has already copied once at the boundary, and the Linux documentation itself places the break-even near 10 KB per send. Without a measurable benefit here, and with a provider that would have to keep every sent buffer until its notification arrives, the service is not provided.

| Bytes per send | `send`, MiB/s (three runs) | `send(MSG_ZEROCOPY)`, MiB/s | Zerocopy sends the kernel copied anyway |
| --- | --- | --- | --- |
| 4,096 | 7,769; 5,979; 8,822 | 2,602; 3,767; 4,527 | all 524,288 per run, less a few in flight |
| 65,536 | 10,578; 10,738; 10,963 | 8,302; 10,007; 8,693 | all 32,768 per run, less a few in flight |
| 1,048,576 | 9,414; 9,424; 9,427 | 7,495; 7,462; 6,612 | all 2,048 per run, less one in flight |

### HTTP/2 and decision 9

*Status:* decision 9 amended by the owner on 2026-11-17 ("Yes, add HTTP/2"); implemented by autonomous agents on the `http2-server` branch, awaiting owner confirmation of the scope below.

Decision 9 now reads: no WebSocket and no connection upgrade in 0.3.0; HTTP/2 is in 0.3.0. The scope:

| Rule | Status |
| --- | --- |
| HTTP/2 over TLS through ALPN: a TLS listener offers `h2` before `http/1.1`, by default, whenever the build has HTTP/2. `--no-http2` offers `http/1.1` only. A client that offers neither is served HTTP/1.1. | implemented |
| Cleartext h2c with prior knowledge only, off by default, enabled by `--h2c` (for use behind a reverse proxy). A plain connection that starts with the HTTP/2 client preface is HTTP/2; any other is HTTP/1.1 on the same listener. | implemented |
| No `Upgrade: h2c` (RFC 9113 deprecates it): such a request is served as HTTP/1.1. No server push (the host never sends `PUSH_PROMISE`). WebSocket and other upgrades stay excluded. | implemented |
| The framing library is nghttp2 1.71.0, pinned in [the server component lock](../../packaging/server-components.lock.toml) with its digest, signature and license, and linked statically. The build option is `NEURA_ENABLE_SERVER_HTTP2`, with `NEURA_NGHTTP2_ROOT`; configure refuses a version other than the locked one. Without the option the host is HTTP/1.1 only and refuses `--h2c`. | implemented |
| Each stream maps to the `Responder` the HTTP/1.1 loop calls, on a thread of its own: the request head at `HEADERS`, checked by the HTTP/1.1 decoder's rules (`check_request_head`: at most 100 fields, one `host`, which `:authority` stands for, a known method, an origin-form target, `Content-Length`, `Expect`); the body from `DATA`, streamed to the worker, with the stream's window reopened only as the responder reads (flow control is the backpressure); the response through a `ResponseWriter` whose writes become `DATA` frames sent as the client's window allows. A buffered response of at most 64 KiB leaves with its head. | implemented |
| S-4 counts connections. S-14 (`--max-streams`) bounds concurrent streams per connection and is sent as `SETTINGS_MAX_CONCURRENT_STREAMS`; a stream past it, counting responders still running after a reset, gets `RST_STREAM(REFUSED_STREAM)`. Each stream is still admitted by S-1 and S-3: 503 with `Retry-After: 1` as an HTTP/2 response. | implemented |
| S-5 applies to the decoded header list, measured as `SETTINGS_MAX_HEADER_LIST_SIZE` measures it (name, value and 32 bytes per field), and is advertised as that setting; past it the answer is 431. The HPACK dynamic table is 4,096 bytes in both directions. | implemented |
| `RST_STREAM` from the client, or the end of the connection, cancels the stream: the host kills its worker at once (the `ResponseWriter::on_cancel` hook) and does not retry it. | implemented |
| On drain (`SIGTERM`) each HTTP/2 connection sends `GOAWAY` with the last stream it accepted; those streams finish, later ones are refused. An idle connection gets `GOAWAY` after the keep-alive time and is closed. | implemented |
| `SIGHUP` TLS reload keeps offering `h2`. The admin listener stays HTTP/1.1. | implemented |

Load over HTTP/2 (2026-11-18, `GET /info`, 8 workers, the host started with `--h2c`; script `http2/h2bench.sh` in the lane's evidence): the `load` scenario of `neura_http2_test_client`, four connections with 8 streams in flight each (32 requests in flight, as B-S2's 32 connections), 30 s after 10 s, answered 4,316 and 3,023 requests per second with no failure (median 6.9 and 10.3 ms, 99th percentile 22 and 28 ms; load average 11.3 and 14.8). B-S2 over HTTP/1.1 against the same host, run alternately, gave 3,682 and 2,361 (load 10.7 and 13.4). The two loaders differ (Python threads for HTTP/1.1, a C++ nghttp2 client for HTTP/2), so the two rates show that HTTP/2 meets the B-S2 rate, not that one protocol is faster. TLS h2 was not put under load; it is covered by the `curl` tests only.

Decisions taken without the owner, within the owner's yes:

1. nghttp2 1.71.0, the current release, rather than the 1.64 the system curl uses.
2. S-14 is 100 by default and at most 1,000; it counts responders that still run, so streams reset at once cannot pile up threads (the "rapid reset" attack).
3. S-5 counts name + value + 32 per field, the same measure the setting uses, and is the value of `SETTINGS_MAX_HEADER_LIST_SIZE`.
4. The HPACK table is the protocol default, 4,096 bytes.
5. An idle HTTP/2 connection is closed after the HTTP/1.1 keep-alive time, with `GOAWAY`.
6. Request trailers are discarded, as the HTTP/1.1 decoder discards them; responses have no trailers (`Std.App` has none).
7. The TLS 1.2 cipher suites RFC 9113 (9.2.2 and Appendix A) prohibits for HTTP/2 are not refused: the TLS context keeps OpenSSL's default TLS 1.2 list, which still contains some of them (CBC suites, for example), so a TLS 1.2 client that negotiates one of them together with `h2` may end the connection with `INADEQUATE_SECURITY`. TLS 1.3 is unaffected. Restricting the TLS 1.2 list would also change HTTP/1.1 clients and is left to the owner; it is listed as a gap.
8. A response that completes before the request body has arrived ends the request stream with `RST_STREAM(NO_ERROR)` (RFC 9113, 8.1), and the rest of the body is discarded.

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
| `Call` | `uint32` call identifier, `uint32` operation, and its arguments: operation 1, configuration: text key; operation 2, SQL: below; operation 3, metric: `uint32` kind (1 counter, 2 gauge), `int64` value, text name |
| `Return` | `uint32` call identifier, `uint32` error code (0 for success); for a configuration, `uint8` found and text value |
| `Call`, operation 2 (SQL) | `uint32` call identifier, `uint32` operation 2, `uint32` SQL operation (1 `pool`, 2 `acquire`, 3 `release`, 4 `begin`, 5 `commit`, 6 `rollback`, 7 `execute`, 8 `query`, 9 `open_cursor`, 10 `fetch`, 11 `close_cursor`), `uint32` argument count, that many encoded values |
| `Return` of an SQL call | `uint32` call identifier, `uint32` 0, `uint8` success, one encoded value (the success payload or the `uint32` error code), text detail |

An encoded value is a `uint64` of bits, a text of bytes, a `uint32` element count and that many encoded values, nested at most 8 deep: the host value of the service's arguments or result. The hello's provider map of a server channel is `neura.app = 1`, `neura.http.server = 1` and `neura.sql = 1`.

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
| A worker is never reused. A reuse would need a proved reset of the NVM, which 0.3.0 does not have; startup cost is measured (budget B-S1), not hidden. | implemented; B-S1 measured (see "Budgets measured"); workers start from a zygote on Linux (departure 17) |
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
| The host speaks HTTP/1.1 with keep-alive and chunked bodies, over TCP and over TLS, and HTTP/2 (decision 9 as amended; see "HTTP/2 and decision 9"). HTTP/3 is terminated by a reverse proxy the operator runs. There is no upgrade and no WebSocket in 0.3.0 (501). | implemented for HTTP/1.1 over TCP and TLS, and for HTTP/2 over TLS (ALPN) and h2c in builds with `NEURA_ENABLE_SERVER_HTTP2` |
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
| Capability groups are added to `nvm::Capabilities`, all denied by default: `ui`, `http_client`, `bridge`, `http_server`, `sql`, `app`, `desktop`. Which target kind may list which is the table of [the overview](Apps030.md#capability-groups). A target's manifest lists the groups and their scope (origins, pool names, operations); the host grants exactly those. A denied service binds and answers 14, as today. | partly: `http_server`, `app` and `sql` |
| Handles (connections, statements, transactions, bodies) are numbers valid for one execution, checked against a generation; a forged or stale one is `invalid_input`. | implemented for SQL connections and cursors: a per-request table of numbers never reused |
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
| `context.sql_pool(name)` returns the `sql.Pool` the manifest declares under that name. The connections live in the host; the worker holds a lease. | implemented: the host's `--sql-pool` and `sql.<name>.*` keys stand in for the manifest |
| `pool.acquire()` returns `Result<sql.Connection>`, and `pool.begin()` a `Result<sql.Transaction>`. Both are closeable and are owned by `using`. A wait beyond `S-10` is `Err`. | implemented, without `using` (departure 15) |
| SQL is a `string`; values are parameters. `execute(sql:, parameters:)` returns the affected row count; `query_one` a row or `Err(NotFound)`; `query_page(sql:, parameters:, limit:)` a `FrozenList<sql.Row>` of at most `limit` and `S-11` rows; `open_cursor` a `sql.Cursor` owned by `using`, fetched in pages. | implemented: parameters trail as a variadic list, the page is an `ImmutableList<sql.Row>`, and a limit above S-11 is `Err(TooLarge)` |
| Parameters are a tuple or array of `sql.Value`: `Null`, `Bool`, `Int(int64)`, `Float(float64)`, `Text(string)`, `Bytes`. Scalars convert implicitly at the call. The provider copies them before the task parks. Placeholders are `$1`, `$2` for both databases; the SQLite provider rewrites them. | implemented: scalars convert through the variadic list; SQLite binds `$n` by name instead of rewriting the text |
| `sql.Row` is immutable. `row.int64("id")`, `row.text("name")`, `row.bool(...)`, `row.float64(...)`, `row.bytes(...)` and their `optional_` forms return a `Result`: a missing column, a `NULL` in a required column, or a type that does not convert exactly is `Err`. No integer is narrowed or rounded silently. | implemented, with `int32` |
| A transaction ends by `commit()` or by `close()`. `close()` without a commit rolls back. After a commit that returned `Ok`, `close()` releases only. | implemented |
| If the connection fails after the commit was sent and before its answer, `commit()` returns `Err(OutcomeUnknown)`. The library does not retry, does not report a rollback, and the lease's connection is discarded. | implemented; no test cuts a connection between `COMMIT` and its answer |
| A serialization failure or deadlock is `Err(Serialization)`; a unique, foreign-key or check failure is `Err(ConstraintViolation)` with the constraint's name. Retrying is the application's decision. | implemented; SQLite gives its message, which names the table and columns, not a constraint |
| Cancelling the scope cancels the running statement: `PQcancel` for PostgreSQL, `sqlite3_interrupt` for SQLite. The call returns `Err(Cancelled)` and the transaction is rolled back. | partly: the request's deadline interrupts a statement (`Err(TimedOut)`, 504); scope cancellation does not reach it (departure 13) |
| A cursor or transaction cannot be held across a generator's suspension, by the rule of the generator milestone. | proposed |
| When a worker exits, the host rolls back its open transactions and returns each connection to the pool after a reset (`DISCARD ALL`; for SQLite, a finalized statement set), or closes it if its state is unknown. | implemented |
| SQLite 3.54.1 is compiled in from the amalgamation, in WAL mode, with `foreign_keys` on and a busy timeout equal to the statement's deadline. Statements run on host threads, never on the listener's. | implemented: on the request's connection thread |
| PostgreSQL uses libpq 18.7 in non-blocking mode against servers 16, 17 and 18, with `sslmode=verify-full` unless the configuration lowers it for loopback. Passwords come from the host's configuration only. | implemented behind `NEURA_ENABLE_POSTGRES`; tested against 16.16, 17.12 and 18.7 |
| Types that convert: SQLite `INTEGER`, `REAL`, `TEXT`, `BLOB`, `NULL`; PostgreSQL `bool`, `int2`, `int4`, `int8`, `float4`, `float8`, `text`, `varchar`, `bytea`, `uuid` and `timestamptz` (as text), and `numeric` (as text). Any other column type is `Err(Unsupported)` naming it. | implemented, with `bpchar` as text |

### Migrations

| Rule | Status |
| --- | --- |
| A pool's migrations are files `NNNN_name.sql` in the directory the manifest names, numbered from 0001 without gaps, one or more statements each. The build copies them into the bundle with their digests; a gap, a repeat or an empty file is `E1044`. | partly: the host reads the directory and checks it at start (departure 16) |
| The host records applied migrations in `neura_migrations(version, name, checksum, applied_at)`. Each is applied in its own transaction, in order, forward only. | implemented |
| `neura-server-host migrate dist/server` applies them and exits. With `migrate = "on_start"` the host applies them before the startup check; with `"manual"` (the default) it refuses to become ready while any is pending. | implemented: `manual` refuses the start |
| An applied migration whose checksum differs from the bundle's refuses the start. Nothing is repaired or re-run. | implemented |
| Concurrent hosts serialize on a PostgreSQL advisory lock, or on SQLite's write lock. | implemented; not tested with two hosts at once |

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
| 5 | `neura.sql.*` with SQLite; `Std.Data.Sql`; transactions and cursors | Cases 10 to 13, 34, 36 to 40 on a temporary database (done: `stdlib-sql`, `server-sql`) |
| 6 | PostgreSQL; cancellation; outcome unknown with a proxy that cuts the connection | Cases 14, 30, 35 against servers 16 and 18 in containers (done for 14 and for cancellation at the deadline, against 16, 17 and 18: `server-sql-postgres`; 30 needs disconnection, 35 the proxy) |
| 7 | Migrations and `E1044` | Cases 15, 41 to 43 (done for 41 and 42 and the manual refusal of 43 in `server-sql`; 15 is not tested) |
| 8 | Configuration, secrets, health, metrics, logs, authentication helpers, trusted proxies | Cases 7, 16, 31 to 33 |
| 9 | Replay of a server image; the load, saturation, drain and sustained budgets | Case 18; budgets B-S1 to B-S6 |

## Gate and acceptance

- HTTP and HTTPS both serve the reference application's contract, with SQLite and with PostgreSQL.
- With 8 requests in flight and one trapping, the other 7 complete, the trapped one is answered or closed once, its leases are revoked, a new worker serves only later requests, startup runs in each worker, and no request is executed twice. The same holds with a commit of unknown outcome, and with the host killed.
- Every limit `S-1` to `S-14` has a test at the limit and one past it.
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
9. No WebSocket or connection upgrade in 0.3.0. HTTP/2 is in 0.3.0 (amended by the owner on 2026-11-17; scope in "HTTP/2 and decision 9").
10. `PendingOperation` gains a platform-neutral wait source; nothing else in the host interface changes.
