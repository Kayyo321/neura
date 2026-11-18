# Server host

`neura-server-host` serves a Neura server image over HTTP/1.1 and HTTPS. It owns the listeners, TLS and the HTTP wire, and runs no application code: each request is executed by a fresh worker process that loads the image, runs the whole program once for that one request, and exits. Nothing a program holds survives between requests; data that must outlive a request belongs in a file or a database the program reaches through its host services. The contract is [the backend design note](../design/Backend030.md); this page describes what the 0.3.0 host does.

The host is built with every build on POSIX systems; this release tests it on Linux x86-64 only, and macOS builds are unqualified. HTTPS needs a build configured with `-DNEURA_ENABLE_SERVER_TLS=ON` and the static OpenSSL 3.5 archives the [server component lock](../../packaging/server-components.lock.toml) pins; a build without it refuses `--tls-listen`.

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
neura build hello.nra -o hello.nvr
neura-server-host --listen 127.0.0.1:8080 hello.nvr
curl http://127.0.0.1:8080/parts
```

`examples/server/hello-server.nra` adds a configuration value, a POST that echoes its body, a handler error and a trap.

| `Std.App` | Meaning |
| --- | --- |
| `run(serve)` | Builds the context, calls `serve`, and returns `Ok` or, after logging the error, `Failure` |
| `Context.serve(handler)` | Receives the assigned request, calls `handler.handle`, sends the response. A handler `Err` is answered with 500 and `{"error":"internal"}` and logged with the request identifier. Fails when there is no assigned request, as under `neura run` |
| `Context.config(key)` | A configuration value of the host, or `None` |
| `Context.log(level, message)` | A JSON log record with the request identifier |
| `Request` | `method`, `target`, `path`, `query`, `headers` (lowercase names, arrival order), `body` (complete), `client`, `scheme`, `request_id`; `header(name)`, `body_text()` |
| `Response` | `new Response(status, headers, body)`, `with_header`; `text`, `html`, `json` (status and text), `empty(status)` |
| `Handler`, `handler_of(function)` | The interface `context.serve` calls; a router implements it |

The host writes `Content-Length` and `Connection` itself and ignores those fields, and `Transfer-Encoding`, in a response. A `HEAD` request reaches the handler as `HEAD`; the host sends the head of its response without the body.

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
| `--max-response-bytes N` | S-7, response body; beyond it, 500 | 8,388,608 |
| `--request-timeout SECONDS` | S-8; beyond it the worker is killed and the answer is 504. The worker's deadline starts when the whole request has been read | 30 |
| `--drain-timeout SECONDS` | S-12, how long a drain waits for requests in flight | 30 |
| `--allow-fs[=prefix,...]`, `--allow-net`, `--allow-env` | Grant the workers that capability group | denied |
| `--record-dir <directory>` | Record each request's host requests to `<request id>.trace` | |
| `--no-startup-check` | Listen without running the startup check | |

The host prints one `listening on` line per listener and then `ready` on standard output.

Before it listens, the host runs one worker with a *startup check*: `Context.serve` answers it with 204 without calling the handler. If `serve` fails or the worker does not exit with status 0, the host does not listen and exits with status 1.

## Requests and failures

The host rejects before any worker runs: a malformed request line or field, a folded field, a missing or repeated `Host`, both `Content-Length` and `Transfer-Encoding` (400); a transfer coding other than `chunked` or an unknown method (501); a head over S-5 (431); a body over S-6 (413). It answers `Expect: 100-continue` before it reads the body. Requests on one connection are served one at a time, in order, with keep-alive.

| Event | Answer |
| --- | --- |
| S-1 workers busy and S-3 waiting | 503 with `Retry-After: 1`, no worker started |
| The handler returns `Err` | 500 `{"error":"internal"}`, the error logged |
| The program traps, or the worker ends without a response | 500 `{"error":"internal"}`; the trap is on standard error and the failure in the log with the request identifier |
| The response body exceeds S-7 | 500 |
| S-8 passes | The worker is killed, 504 `{"error":"timeout"}` |
| The host is draining | 503 |

A failed request affects no other request: each runs in its own process.

Each worker runs the image with the default limits of `neura run` (100,000,000 steps, 10,000 frames, and the default heap); the host has no option to change them in this release. Each worker always has the console, clock and entropy services and the application services. Files, network and environment are denied unless the host grants them; native code is always denied. A worker's console output goes to the host's standard error.

## Configuration, logs and signals

`Context.config(key)` reads the environment variable `<prefix><key>` when `--config-env-prefix` is set, then the `--config` values, then the configuration file. The worker asks the host for each value over its channel; it never receives the whole configuration.

The host and its workers write JSON lines on standard error: `time_ms`, `level`, `request_id` when there is one, `message`, and named fields. A record longer than 16 KiB keeps its first 2 KiB of message and gains `"truncated":"true"`.

`SIGTERM` and `SIGINT` stop accepting, answer 503 to new requests, wait up to S-12 for the requests in flight, kill the remaining workers and exit (status 0, or 1 when a request was still running). `SIGHUP` rereads the certificate and key; a failed reload keeps the previous pair. When the host itself is killed, its workers see their channel close and exit.

## Record and replay

With `--record-dir`, each worker records its execution's host requests and completions. The image then runs without a host, a socket or a client:

```text
neura run --replay traces/<request id>.trace hello.nvr
```

Under `neura run` without a trace, a server image binds its application services but every call fails with `CapabilityDenied`, and `Std.App.run` returns `Failure` with the message `run this image with neura-server-host`.

## Not in 0.3.0

Streamed request and response bodies (the host buffers both within S-6 and S-7), HTTP/2, HTTP/3 and WebSocket upgrades, the admin listener (`/livez`, `/readyz`, `/metrics`), SQL pools and migrations, trusted proxies, and running a project bundle with `neura run`. See the [implementation status](../design/Backend030.md#implementation-status-2026-11-17).
