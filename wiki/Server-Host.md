<!-- Generated from docs/runtime/ServerHost.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Server host

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

`neura-server-host` serves a Neura server image over HTTP/1.1 and HTTPS. It owns the listeners, TLS and the HTTP wire, and runs no application code: each request is executed by a fresh worker process that loads the image, runs the whole program once for that one request, and exits. Nothing a program holds survives between requests; data that must outlive a request belongs in a file or a database the program reaches through its host services. The contract is [the backend design note](https://github.com/Kayyo321/neura/blob/main/docs/design/Backend030.md); this page describes what the 0.3.0 host does.

The host is built with every build on POSIX systems; this release tests it on Linux x86-64 only, and macOS builds are unqualified. HTTPS needs a build configured with `-DNEURA_ENABLE_SERVER_TLS=ON` and the static OpenSSL 3.5 archives the [server component lock](https://github.com/Kayyo321/neura/blob/main/packaging/server-components.lock.toml) pins; a build without it refuses `--tls-listen`.

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

`neura serve` builds the program beside its source (`hello.nvr`) and replaces itself with `neura-server-host`, found beside `neura` or on `PATH`, listening on `127.0.0.1:8080` unless a `--listen` or `--tls-listen` is given. It passes every other option to the host, and also takes a built image: `neura serve hello.nvr --listen 0.0.0.0:80 --max-workers 16`. It is the form of `neura run dist/server` this release has: project bundles do not exist yet. The two-step form is the same:

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
| `Context.sql_pool(name)` | The host's [SQL pool](Sql) of that name; the host owns the connections and rolls back and returns what a request leaves open |
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
| `--config <key>=<value>` | A configuration value. Repeatable | |
| `--config-file <file>` | `key=value` lines; `#` starts a comment | |
| `--config-env-prefix <prefix>` | The environment variable `<prefix><key>` overrides the file and `--config` | |
| `--max-workers N` | S-1, requests executing at once (1 to 1,024) | 8 |
| `--ready-workers N` | S-2, workers started ahead that have loaded and verified the image and executed nothing (0 to 64) | 2 |
| `--max-queue N` | S-3, requests waiting for a worker; beyond it, 503 with `Retry-After: 1` | 64 |
| `--max-connections N` | S-4, open connections | 1,024 |
| `--max-head-bytes N` | S-5, request line and fields; beyond it, 431 | 16,384 |
| `--max-body-bytes N` | S-6, request body after decoding; beyond it, 413 | 1,048,576 |
| `--max-response-bytes N` | S-7, a complete (buffered) response body; beyond it, 500. A streamed body is not buffered and not counted | 8,388,608 |
| `--request-timeout SECONDS` | S-8, from the end of the request head to the last byte of the response; beyond it the worker is killed, and the answer is 504 if nothing was sent, or the connection is closed | 30 |
| `--drain-timeout SECONDS` | S-12, how long a drain waits for requests in flight | 30 |
| `--max-steps N` | NVM steps one request's execution may take (1 to 2^62); past it the execution traps and the answer is 500 | 100,000,000 |
| `--worker-tier 0\|1\|2`, `--worker-jit off\|on\|auto` | The highest execution tier and the native compilation of each worker, as `neura run --tier` and `--jit` | 2, auto |
| `--admin-listen <address>:<port>` | The admin listener (below). Repeatable; give it a loopback address | none |
| `--allow-fs[=prefix,...]`, `--allow-net`, `--allow-env` | Grant the workers that capability group | denied |
| `--record-dir <directory>` | Record each request's host requests to `<request id>.trace` | |
| `--no-startup-check` | Listen without running the startup check | |
| `--sql-pool <name>=<url>` | An SQL pool for `Std.Data.Sql`: `sqlite:<path>`, `sqlite::memory:`, or a `postgresql://` URL without a password. Its settings and password are `sql.<name>.*` configuration keys ([SQL](Sql#configuring-pools)). Repeatable | |

The host prints one `admin on` line per admin listener, one `listening on` line per listener, and then `ready` on standard output.

`neura-server-host migrate [options] <image.nvr>` applies the pending [migrations](Sql#migrations) of the configured pools and exits without listening. Before a host listens, it opens one connection of each pool and checks its migrations: with `sql.<name>.migrate=on_start` it applies them, and otherwise it refuses to start while any is pending.

Before it listens, the host runs one worker with a *startup check*: `Context.serve` answers it with 204 without calling the handler. If `serve` fails or the worker does not exit with status 0, the host does not listen and exits with status 1.

## Requests and failures

The host rejects before any worker runs: a malformed request line or field, a folded field, a missing or repeated `Host`, both `Content-Length` and `Transfer-Encoding` (400); a transfer coding other than `chunked` or an unknown method (501); a head over S-5 (431); a `Content-Length` over S-6 (413). It answers `Expect: 100-continue` before it reads the body. Requests on one connection are served one at a time, in order, with keep-alive.

A request is given to a worker at the end of its head, and its body streams to the worker as the client sends it; the worker starts running at once. A chunked body that grows past S-6, or whose chunk syntax is wrong, is found while it streams: the worker is killed and the answer is 413 or 400 if nothing was sent. The response streams back to the client as the worker produces it, once the request body has been received; a handler that answers before reading the body has the rest read and discarded, and the connection is kept. A client that stops reading slows the worker down: each write waits for the client (backpressure), within S-8.

A ready worker whose process has ended is never given a request: the host discards it and uses another, and logs it. Because a worker runs no Neura code before it takes its assignment, this never runs a request twice; a worker that ends after its assignment is not replaced, and its request is answered as below.

| Event | Answer |
| --- | --- |
| S-1 workers busy and S-3 waiting | 503 with `Retry-After: 1`, no worker started |
| The handler returns `Err` | 500 `{"error":"internal"}`, the error logged |
| The program traps, or the worker ends without a response | 500 `{"error":"internal"}`; the trap is on standard error and the failure in the log with the request identifier |
| A complete response body exceeds S-7 | 500 |
| The execution passes `--max-steps` | 500; the step-limit trap is on standard error |
| S-8 passes | The worker is killed, 504 `{"error":"timeout"}`, or, inside a streamed response, the connection is closed |
| The program traps or a producer fails inside a streamed response | The connection is closed without the last chunk: the client sees a truncated body |
| The host is draining | 503 |

A failed request affects no other request: each runs in its own process.

Each worker runs the image with the limits of `neura run`: `--max-steps` (default 100,000,000), 10,000 frames, and the default heap. The step limit is one value for every request: neither design note defines a per-route limit, and the host does not know the routes, which the program's router resolves. Each worker always has the console, clock and entropy services and the application services. Files, network and environment are denied unless the host grants them; native code is always denied. A worker's console output goes to the host's standard error.

## Configuration, logs and signals

`Context.config(key)` reads the environment variable `<prefix><key>` when `--config-env-prefix` is set, then the `--config` values, then the configuration file. The worker asks the host for each value over its channel; it never receives the whole configuration. Keys that start with `sql.` are pool settings: the host removes them before any worker starts and answers `None` for them.

The host and its workers write JSON lines on standard error: `time_ms`, `level`, `request_id` when there is one, `message`, and named fields. A record longer than 16 KiB keeps its first 2 KiB of message and gains `"truncated":"true"`.

`SIGTERM` and `SIGINT` stop accepting, answer 503 to new requests, wait up to S-12 for the requests in flight, kill the remaining workers and exit (status 0, or 1 when a request was still running). `SIGHUP` rereads the certificate and key; a failed reload keeps the previous pair. When the host itself is killed, its workers see their channel close and exit.

## The admin listener

With `--admin-listen 127.0.0.1:9090` the host answers, outside worker admission and before the startup check ends:

| Request | Answer |
| --- | --- |
| `GET /livez` | 200 `ok` while the host process runs |
| `GET /readyz` | 200 `ready` after the startup check passed and the listeners are bound; 503 `starting` before, 503 `draining` during the drain |
| `GET /metrics` | The Prometheus text format (version 0.0.4): `neura_http_responses_total` by status class, `neura_http_rejected_total`, `neura_http_timeouts_total`, `neura_worker_starts_total`, `neura_worker_abnormal_exits_total`, `neura_worker_retries_total`, the gauges `neura_http_in_flight`, `neura_workers_running`, `neura_workers_ready`, `neura_requests_waiting`, `neura_ready`, and every counter and gauge the program set with `Context.count` and `Context.gauge` |

Another method is 405, another path 404. An application metric name is `[A-Za-z_:][A-Za-z0-9_:]*` of at most 128 bytes, keeps the kind it was first used with, and the host keeps at most 1,024 of them; a name past that answers `TooLarge`. The listener has no authentication: bind it to loopback or a private network.

## Record and replay

With `--record-dir`, each worker records its execution's host requests and completions. The image then runs without a host, a socket or a client:

```text
neura run --replay traces/<request id>.trace hello.nvr
```

Under `neura run` without a trace, a server image binds its application services but every call fails with `CapabilityDenied`, and `Std.App.run` returns `Failure` with the message `run this image with neura-server-host or neura serve`.

## Not in 0.3.0

HTTP/2, HTTP/3 and WebSocket upgrades (approved decision 9 of the design note), trusted proxies, a per-route step limit, and running a project bundle with `neura run`. See the [implementation status](https://github.com/Kayyo321/neura/blob/main/docs/design/Backend030.md#implementation-status-2026-11-17).
