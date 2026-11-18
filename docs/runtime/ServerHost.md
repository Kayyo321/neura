# Server host

`neura-server-host` serves a Neura server image over HTTP/1.1, HTTPS and, in builds with it, HTTP/2 ([below](#http2)). It owns the listeners, TLS and the HTTP wire, and runs no application code: each request is executed by a fresh worker process that loads the image, runs the whole program once for that one request, and exits. Nothing a program holds survives between requests; data that must outlive a request belongs in a file or a database the program reaches through its host services. The contract is [the backend design note](../design/Backend030.md); this page describes what the 0.3.0 host does.

The host is built with every build on POSIX systems. The [release archives](#release-archives) of every target have HTTPS, HTTP/2 and the PostgreSQL provider. A build from source has them only when it is configured with them: HTTPS needs a build configured with `-DNEURA_ENABLE_SERVER_TLS=ON` and the static OpenSSL 3.5 archives the [server component lock](../../packaging/server-components.lock.toml) pins; a build without it refuses `--tls-listen`. HTTP/2 also needs `-DNEURA_ENABLE_SERVER_HTTP2=ON` with `-DNEURA_NGHTTP2_ROOT=<prefix>`, the install prefix of the static nghttp2 build the lock pins.

## A server image

A server image is an ordinary program whose `main` calls `Std.App.run` with its `serve` function. `serve` receives a `Context` and hands a handler to `context.serve`, which receives the request the host assigned, calls the handler, and sends the response.

<!-- example: check -->
```neura
let app = import:Std.App;
let program = import:Std.Program;
let.promote _ = import:Std.Core.Result;
let.promote _ = import:Std.Core.Unit;

let Hello = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode => app.run(serve);

    let serve = func(context: app.Context) -> Result<Unit> =>
        context.serve(app.handler_of(new func(request: app.Request) -> Result<app.Response> =>
            Result<app.Response>.Ok(app.text(200, "hello from " + request.path + "\n"))));
);
```

```text
neura serve hello.nra
curl http://127.0.0.1:8080/parts
```

`neura serve` builds the program beside its source (`hello.nvr`) and replaces itself with `neura-server-host`, found beside `neura-run`, which serves the command, or else on `PATH`, listening on `127.0.0.1:8080` unless a `--listen` or `--tls-listen` is given. It passes every other option to the host, and also takes a built image: `neura serve hello.nvr --listen 0.0.0.0:80 --max-workers 16`. It is the form of `neura run dist/server` this release has: project bundles do not exist yet. The two-step form is the same:

```text
neura build hello.nra -o hello.nvr
neura-server-host --listen 127.0.0.1:8080 hello.nvr
```

`examples/server/hello-server.nra` adds a configuration value, a POST that echoes its body, a handler error and a trap.

| `Std.App` | Meaning |
| --- | --- |
| `run(serve)` | Builds the context, calls `serve`, and returns `Ok` or, after logging the error, `Failure` |
| `Context.serve(handler)` | Receives the assigned request, reads its whole body, calls `handler.handle`, sends the response. A handler `Err` is answered with 500 and `{"error":"internal"}` and logged with the request identifier. Fails when there is no assigned request, as under `neura run` |
| `Context.serve_streamed(handler)` | The same, but calls a `StreamingHandler` at once with an empty `Request.body` and a `BodyReader`, so a large upload is read as it arrives. What the handler leaves unread is discarded |
| `Context.config(key)` | A configuration value of the host, or `None` |
| `Context.sql_pool(name)` | The host's [SQL pool](Sql.md) of that name; the host owns the connections and rolls back and returns what a request leaves open |
| `Context.log(level, message)` | A JSON log record with the request identifier |
| `Context.count(name, amount)`, `Context.gauge(name, value)` | Add to a counter or set a gauge of the host, shown on the admin listener's `/metrics` |
| `Request` | `method`, `target`, `path`, `query`, `headers` (lowercase names, arrival order), `body` (complete under `serve`), `client`, `scheme`, `request_id`; `header(name)`, `body_text()` |
| `Response` | `new Response(status, headers, body)`, `with_header`; `text`, `html`, `json` (status and text), `empty(status)` |
| `stream(status, headers, producer)`, `Response.with_stream(producer)` | A response whose body `producer` writes through a `BodyWriter` while the client receives it |
| `BodyReader` | `read(max)` (empty at the end), `read_all()` |
| `BodyWriter` | `write(bytes)`, `write_text(text)`: each is sent at once as one chunk, and waits while the client is slow |
| `Handler`, `handler_of(function)`; `StreamingHandler`, `streaming_handler_of(function)` | The interfaces `serve` and `serve_streamed` call; a router implements `Handler` |

The host writes `Content-Length`, `Transfer-Encoding` and `Connection` itself and ignores those fields in a response. A complete body is sent with `Content-Length`; a streamed one with chunked transfer coding, each `write` as one chunk. A `HEAD` request reaches the handler as `HEAD`; the host sends the head of its response without the body.

<!-- example: check -->
```neura
let app = import:Std.App;
let program = import:Std.Program;
let.promote _ = import:Std.Core.Result;
let.promote _ = import:Std.Core.Unit;

let Events = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode => app.run(serve);

    let serve = func(context: app.Context) -> Result<Unit> =>
        context.serve(app.handler_of(new func(request: app.Request) -> Result<app.Response> => events()));

    /// @brief Three server-sent events, each sent as soon as it is written.
    let events = func -> Result<app.Response> {
        last fields: [string, string][list] = new;
        fields.append(["Content-Type", "text/event-stream"]);
        fields.append(["Cache-Control", "no-store"]);
        return Result<app.Response>.Ok(app.stream(200, fields, new func(writer: app.BodyWriter) -> Result<Unit> {
            for (let n = 1; n <= 3; n++) {
                match (writer.write_text("data: event " + n.to_string() + "\n\n")) {
                    Result.Err(problem) => {
                        return Result<Unit>.Err(problem);
                    },
                    Result.Ok(_) => {},
                }
            }
            return Result<Unit>.Ok(Unit.Value);
        }));
    };
);
```

## Running the host

```text
neura-server-host [options] <image.nvr>
```

| Option | Meaning | Default |
| --- | --- | --- |
| `--listen <address>:<port>` | A plain HTTP listener on a numeric IPv4 or IPv6 address; port 0 asks for a free one. Repeatable | |
| `--tls-listen <address>:<port>` | An HTTPS listener (TLS 1.2 and 1.3). Repeatable | |
| `--cert <file>`, `--key <file>` | PEM certificate chain and private key for the TLS listeners. A missing file or a key that does not match fails the start | |
| `--no-http2` | TLS listeners offer only `http/1.1` by ALPN (builds with HTTP/2, below) | `h2` and `http/1.1` |
| `--h2c` | Plain listeners also accept HTTP/2 with prior knowledge (builds with HTTP/2) | off |
| `--max-streams N` | S-14, concurrent HTTP/2 streams on one connection (1 to 1,000) | 100 |
| `--config <key>=<value>` | A configuration value. Repeatable | |
| `--config-file <file>` | `key=value` lines; `#` starts a comment | |
| `--config-env-prefix <prefix>` | The environment variable `<prefix><key>` overrides the file and `--config` | |
| `--max-workers N` | S-1, requests executing at once (1 to 1,024). A request's worker holds its slot until the host has reaped it | 8 |
| `--ready-workers N` | S-2, workers started ahead that have loaded and verified the image and executed nothing (0 to 64) | 2 |
| `--max-queue N` | S-3, requests waiting for a worker; beyond it, 503 with `Retry-After: 1`. With 0, a request that follows a response at once can find the slot still held by the exiting worker and be refused | 64 |
| `--max-connections N` | S-4, open connections | 1,024 |
| `--max-head-bytes N` | S-5, request line and fields; beyond it, 431 | 16,384 |
| `--max-body-bytes N` | S-6, request body after decoding; beyond it, 413 | 1,048,576 |
| `--max-response-bytes N` | S-7, a complete (buffered) response body; beyond it, 500. A streamed body is not buffered and not counted | 8,388,608 |
| `--request-timeout SECONDS` | S-8, from the end of the request head to the last byte of the response; beyond it the worker is killed, and the answer is 504 if nothing was sent, or the connection is closed | 30 |
| `--drain-timeout SECONDS` | S-12, how long a drain waits for requests in flight | 30 |
| `--max-steps N` | NVM steps one request's execution may take (1 to 2^62); past it the execution traps and the answer is 500 | 100,000,000 |
| `--worker-tier 0\|1\|2`, `--worker-jit off\|on\|auto` | The highest execution tier and the native compilation of each worker, as `neura run --tier` and `--jit` | 2, auto |
| `--worker-start zygote\|exec` | How workers start (below): forked from a zygote that loaded the image once, or each with `exec` | `zygote` on Linux, `exec` elsewhere |
| `--admin-listen <address>:<port>` | The admin listener (below). Repeatable; give it a loopback address | none |
| `--allow-fs[=prefix,...]`, `--allow-net`, `--allow-env` | Grant the workers that capability group | denied |
| `--record-dir <directory>` | Record each request's host requests to `<request id>.trace` | |
| `--no-startup-check` | Listen without running the startup check | |
| `--sql-pool <name>=<url>` | An SQL pool for `Std.Data.Sql`: `sqlite:<path>`, `sqlite::memory:`, or a `postgresql://` URL without a password. Its settings and password are `sql.<name>.*` configuration keys ([SQL](Sql.md#configuring-pools)). Repeatable | |

The host prints one `admin on` line per admin listener, one `listening on` line per listener, and then `ready` on standard output.

`neura-server-host migrate [options] <image.nvr>` applies the pending [migrations](Sql.md#migrations) of the configured pools and exits without listening. Before a host listens, it opens one connection of each pool and checks its migrations: with `sql.<name>.migrate=on_start` it applies them, and otherwise it refuses to start while any is pending.

Before it listens, the host runs one worker with a *startup check*: `Context.serve` answers it with 204 without calling the handler. If `serve` fails or the worker does not exit with status 0, the host does not listen and exits with status 1.

## Worker start

Every request still gets a new process that has run no Neura code and serves that one request. What differs is how the process gets its loaded image:

- **zygote** (Linux): at startup the host starts one helper process, the zygote (`neura-zygote` in `ps`), which reads the image, computes its SHA-256, loads it and verifies it once, and then waits. For each worker the host passes it the worker's end of a new channel and the zygote forks; the fork is made with `CLONE_PARENT`, so the worker is a child of the host, which waits for it, limits it and kills it like any other worker. The zygote has one thread, runs no Neura code and holds no request data, so a forked worker starts in the same state a worker started with `exec` reaches after its verification. A zygote that ends is replaced, up to three times; after that, or when one cannot start, the host starts workers with `exec` and logs it.
- **exec**: each worker is `neura-server-host --worker` started with `posix_spawn`, and reads, hashes, loads and verifies the image itself.

Forked workers share the zygote's address-space layout: address-space layout randomization differs between the host and its workers, but not from one worker to the next. A program that must not share its layout with other requests' workers runs with `--worker-start exec`.

A worker holds its S-1 slot from its assignment until the host has reaped it, so the host's workers, exited or not, never outnumber S-1 plus S-2, and with the zygote the host's process tree never exceeds S-1 + S-2 + 2 processes. The host learns that a worker exited from its `pidfd` (Linux 5.3 and later; elsewhere it looks every 2 ms).

## Requests and failures

The host rejects before any worker runs: a malformed request line or field, a folded field, a missing or repeated `Host`, both `Content-Length` and `Transfer-Encoding` (400); a transfer coding other than `chunked` or an unknown method (501); a head over S-5 (431); a `Content-Length` over S-6 (413). It answers `Expect: 100-continue` before it reads the body. Requests on one connection are served one at a time, in order, with keep-alive.

A request is given to a worker at the end of its head, and its body streams to the worker as the client sends it; the worker starts running at once. A chunked body that grows past S-6, or whose chunk syntax is wrong, is found while it streams: the worker is killed and the answer is 413 or 400 if nothing was sent. The response streams back to the client as the worker produces it, once the request body has been received; a handler that answers before reading the body has the rest read and discarded, and the connection is kept. A client that stops reading slows the worker down: each write waits for the client (backpressure), within S-8.

A ready worker whose process has ended is never given a request: the host discards it and uses another, and logs it. The same holds for a worker whose channel refuses the assignment itself; when its replacement refuses too, the answer is 500 and no third worker is tried. Because a worker runs no Neura code before it takes its assignment, this never runs a request twice; a worker that ends after its assignment is not replaced, and its request is answered as below.

| Event | Answer |
| --- | --- |
| S-1 workers busy and S-3 waiting | 503 with `Retry-After: 1`, no worker started |
| The handler returns `Err` | 500 `{"error":"internal"}`, the error logged |
| The program traps, or the worker ends without a response | 500 `{"error":"internal"}`; the trap is on standard error and the failure in the log with the request identifier |
| A complete response body exceeds S-7 | 500 |
| The execution passes `--max-steps` | 500; the step-limit trap is on standard error |
| S-8 passes | The worker is killed, 504 `{"error":"timeout"}`, or, inside a streamed response, the connection is closed. A host call (an SQL statement) that the deadline interrupts ends the request the same way, even when the worker answers or exits first |
| The program traps or a producer fails inside a streamed response | The connection is closed without the last chunk: the client sees a truncated body |
| The host is draining | 503 |

A failed request affects no other request: each runs in its own process.

Each worker runs the image with the limits of `neura run`: `--max-steps` (default 100,000,000), 10,000 frames, and the default heap. The step limit is one value for every request: neither design note defines a per-route limit, and the host does not know the routes, which the program's router resolves. Each worker always has the console, clock and entropy services and the application services. Files, network and environment are denied unless the host grants them; native code is always denied. A worker's console output goes to the host's standard error.

## HTTP/2

In builds with `NEURA_ENABLE_SERVER_HTTP2` (the pinned nghttp2 1.71.0, linked statically) the host also speaks HTTP/2:

- On a TLS listener, ALPN offers `h2` before `http/1.1`; a client that picks `h2` is served HTTP/2, one that picks `http/1.1` or offers neither, HTTP/1.1. `--no-http2` offers `http/1.1` only.
- On a plain listener, `--h2c` accepts HTTP/2 with prior knowledge: a connection that starts with the HTTP/2 client preface is HTTP/2, any other HTTP/1.1. Use it behind a reverse proxy that speaks h2c to the host. `Upgrade: h2c` is not supported (the request is served as HTTP/1.1), and the host never pushes.
- Each stream is one request, served exactly as an HTTP/1.1 request is: the same admission (S-1, S-3; 503 as an HTTP/2 response), deadline, limits, worker and answers. Streams on one connection run at once, up to S-14 (`--max-streams`); a stream past it is refused with `RST_STREAM(REFUSED_STREAM)`, which a client may retry.
- S-5 bounds the decoded header list (each field counts its name, its value and 32 bytes), advertised as `SETTINGS_MAX_HEADER_LIST_SIZE`; past it the answer is 431. The HPACK table is 4,096 bytes.
- Flow control is the backpressure: a client that stops reading stops that stream's worker, within S-8, and not the other streams. The host reopens a stream's receive window only as its worker reads the body.
- A client that resets a stream (`RST_STREAM`), or closes the connection, before the stream's response has ended cancels the request: its worker is killed at once and the request is not retried. A connection that closes after a response has ended leaves that worker to finish, as under HTTP/1.1 (writing its `--record-dir` trace, for one).
- On `SIGTERM` each connection sends `GOAWAY`; the streams already accepted finish within S-12, new ones are refused. An idle connection is closed with `GOAWAY` after the keep-alive time. `SIGHUP` reloads the certificate and keeps offering `h2`. The admin listener speaks HTTP/1.1 only.

A build without the option speaks HTTP/1.1 only: TLS listeners select `http/1.1`, and `--h2c` fails the start.

## Release archives

The release archives of every target (Linux and macOS, x86-64 and arm64) link the three server components statically: OpenSSL 3.5.9 for TLS, nghttp2 1.71.0 for HTTP/2, and libpq 18.7 for the [PostgreSQL provider](Sql.md#building). [`packaging/build-server-components.sh`](../../packaging/build-server-components.sh) builds them from the sources [the server component lock](../../packaging/server-components.lock.toml) pins, after checking each archive and each licence text against the lock's SHA-256, as static, position-independent libraries with hidden visibility; the release workflow, the Tier 1 build and a local release build run the same script and the same configure options:

```text
sh packaging/build-server-components.sh <prefix>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DNEURA_ENABLE_SERVER_TLS=ON -DOPENSSL_ROOT_DIR=<prefix>/openssl \
  -DNEURA_ENABLE_SERVER_HTTP2=ON -DNEURA_NGHTTP2_ROOT=<prefix>/nghttp2 \
  -DNEURA_ENABLE_POSTGRES=ON -DNEURA_LIBPQ_SOURCE=<prefix>/postgresql -DNEURA_LIBPQ_BUILD=<prefix>/postgresql \
  -DNEURA_COMPONENT_LICENSES=<prefix>/licenses
```

`NEURA_COMPONENT_LICENSES` installs the licence texts (OpenSSL's Apache License 2.0, nghttp2's MIT licence and the PostgreSQL licence) in `share/doc/neura/third-party` with the core component; a `NEURA_PORTABLE_RELEASE` build that links a component refuses to configure without its text. The release workflow fails when the build links an OpenSSL or nghttp2 from anywhere else, and when a tool or `libneura-core` exports a function of the components; it runs the host's tests (`server-host` with TLS and HTTP/2, `server-host-h2c`, `server-sql`) on every target and `server-sql-postgres` on Linux, whose runners have Docker. On macOS the TLS and HTTP/2 tests pass, the cases that need `/proc` or the Linux worker zygote are skipped, and so is the refused-assignment case, whose test hook refuses in a way only Linux reports to the host, and the PostgreSQL provider is built but not tested, for want of Docker; and it serves the example over HTTPS with HTTP/2 and over h2c from each installed archive ([check 15](../ReleaseProcess.md#what-the-release-workflow-checks)).

`neura-server-host` and `libneura-core` each hold a copy of OpenSSL: the host's TLS listeners use its own, and `Std.Net.Tls` and libpq, in `libneura-core`, use the library's. On Linux the native launcher holds a third, for `Std.Net.Tls` in executables that `neura build --native` makes, which accounts for most of the 6.8 MB by which a native Hello World on x86-64 outgrows 0.2.1-rc.3's (11,953,251 bytes against 5,127,992; the static OpenSSL libraries hold 6.5 MB of code and data). The copies are why the SDK archive outgrows the [size budget](../design/Release030.md) of 0.3.0. The OpenSSL of the archives reads `/etc/ssl/openssl.cnf` and takes its default trust anchors from `/etc/ssl` ([installation](../Install.md#supported-platforms)).

## Configuration, logs and signals

`Context.config(key)` reads the environment variable `<prefix><key>` when `--config-env-prefix` is set, then the `--config` values, then the configuration file. The worker asks the host for each value over its channel; it never receives the whole configuration. Keys that start with `sql.` are pool settings: the host removes them before any worker starts and answers `None` for them.

The host and its workers write JSON lines on standard error: `time_ms`, `level`, `request_id` when there is one, `message`, and named fields. A record longer than 16 KiB keeps its first 2 KiB of message and gains `"truncated":"true"`.

`SIGTERM` and `SIGINT` stop accepting, answer 503 to new requests, wait up to S-12 for the requests in flight and for the worker of each answered request to exit (with `--record-dir`, after its trace is written; a worker past its grace period is still killed), kill the remaining workers and exit (status 0, or 1 when a request was still running). `SIGHUP` rereads the certificate and key; a failed reload keeps the previous pair. When the host itself is killed, its workers see their channel close and exit.

## The admin listener

With `--admin-listen 127.0.0.1:9090` the host answers, outside worker admission and before the startup check ends:

| Request | Answer |
| --- | --- |
| `GET /livez` | 200 `ok` while the host process runs |
| `GET /readyz` | 200 `ready` after the startup check passed and the listeners are bound; 503 `starting` before, 503 `draining` during the drain |
| `GET /metrics` | The Prometheus text format (version 0.0.4): `neura_http_responses_total` by status class, `neura_http_rejected_total`, `neura_http_timeouts_total`, `neura_worker_starts_total`, `neura_worker_abnormal_exits_total`, `neura_worker_retries_total`, the gauges `neura_http_in_flight`, `neura_workers_running` (slots in use, each held until its worker is reaped), `neura_workers_ready`, `neura_requests_waiting`, `neura_ready`, and every counter and gauge the program set with `Context.count` and `Context.gauge` |

Another method is 405, another path 404. An application metric name is `[A-Za-z_:][A-Za-z0-9_:]*` of at most 128 bytes, keeps the kind it was first used with, and the host keeps at most 1,024 of them; a name past that answers `TooLarge`. The listener has no authentication: bind it to loopback or a private network.

## Record and replay

With `--record-dir`, each worker records its execution's host requests and completions. The image then runs without a host, a socket or a client:

```text
neura run --replay traces/<request id>.trace hello.nvr
```

Under `neura run` without a trace, a server image binds its application services but every call fails with `CapabilityDenied`, and `Std.App.run` returns `Failure` with the message `run this image with neura-server-host or neura serve`.

## Not in 0.3.0

HTTP/3, WebSocket and other upgrades, `Upgrade: h2c` and server push (approved decision 9 of the design note, as amended for HTTP/2), trusted proxies, a per-route step limit, and running a project bundle with `neura run`. See the [implementation status](../design/Backend030.md#implementation-status-2026-11-17).
