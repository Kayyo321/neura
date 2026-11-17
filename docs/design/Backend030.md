# Neura 0.3.0: backend

This note fixes the backend contracts before they are implemented: the server host and its request workers, `Std.Net.Http` routes and middleware, the provider ABI that every application host service follows, TLS, `Std.Data.Sql` with SQLite and PostgreSQL, migrations, and operation. It is one of the notes of [the application platform](Apps030.md), which owns the limits (`S-1` and the like), budgets and code ranges referred to here. Rows move to `docs/apps/Backend.md` and `docs/runtime/Providers.md` in the change that implements them.

The owner approved the A0 design decisions on 2026-11-17. Rows marked *proposed* await implementation and qualification in A4. Later changes to a row are made here, with the reason.

Nothing below exists in the repository: it has TCP streams and listeners and no HTTP, TLS or SQL code ([inventory](Apps030.md#inventory-of-reusable-code)). The note adds no syntax and depends, by name, on records, `using`, `?` on a `Result`, constructor shorthand and named arguments.

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
| Each request is executed by one worker: an operating-system process that loads the server image into a fresh NVM, runs the whole program from its startup, handles that one request, and exits, whether it answers, fails or traps. | proposed |
| `context.serve(router)` takes the one request the host assigned, dispatches it, sends the response and returns. The program is written without a loop and holds nothing between requests. Data that must outlive a request is in the database. | proposed |
| The host keeps `S-2` ready workers that have loaded and verified the image and bound its services and have executed nothing: no global initializer, no `static scope`, no entry. A request is given to a ready worker, or to a new one, up to `S-1`; beyond that it waits, up to `S-3`. | proposed |
| A worker is never reused. A reuse would need a proved reset of the NVM, which 0.3.0 does not have; startup cost is measured (budget B-S1), not hidden. | proposed |
| A worker is the runner (`src/runner/runner.cpp`) with a host registry whose services forward to the host over one inherited channel. It has no listener, no database connection and no file or network capability of its own beyond what the manifest grants the target. | proposed |
| A trap ends that worker and every task in it. The host answers 500 with the framework envelope if no byte of the response was sent, and otherwise closes the connection. It revokes the worker's leases, logs the request identifier and the trap, and starts a ready worker for later requests. Other requests are unaffected. | proposed |
| Nothing is replayed: not a request whose worker died, not a statement, not a commit. | proposed |
| If the host itself fails, the application ends. Neither it nor `neurad` restarts it; a deployment's supervisor may, by its own policy. | proposed |
| The host runs in the foreground: `neura run dist/server` or `neura-server-host dist/server`. Under `neura run --runtime=required` the whole tree is one application of the service. | proposed |
| Before it accepts, the host runs one worker with a *startup check* request. `serve` runs as usual, the router answers the check without calling a route, and migrations and configuration are verified. Readiness is true only after it succeeds. | proposed |

### Request-worker protocol

One channel per worker: a socket pair on POSIX. Frames are a 4-byte little-endian length, a 1-byte kind, and a body; no frame exceeds `C-2` except body chunks, which are at most 64 KiB.

| Kind | Direction | Body |
| --- | --- | --- |
| `Hello` | both | Protocol version (1), image digest, provider versions |
| `Assign` | host to worker | Request identifier, deadline, peer address, method, target, headers, body length, the leases granted |
| `BodyChunk`, `BodyEnd` | both | Bytes of a request or response body, with backpressure by credit |
| `Call`, `Return` | worker to host, host to worker | A host service request by import name and version, and its completion |
| `Cancel` | host to worker | The request's deadline passed, the client left, or the host is draining |
| `Head` | worker to host | Status and headers of the response |
| `Done` | worker to host | The response is complete |

A frame the receiver cannot decode, a version it does not speak, or a digest that differs ends the worker before `Assign`.

## HTTP

| Rule | Status |
| --- | --- |
| The host speaks HTTP/1.1 with keep-alive and chunked bodies, over TCP and over TLS. HTTP/2 and HTTP/3 are terminated by a reverse proxy the operator runs. There is no upgrade and no WebSocket in 0.3.0 (501). | proposed |
| The host rejects before any worker: a head over `S-5` (431), more than 100 header fields (431), a body over `S-6` (413), both `Content-Length` and `Transfer-Encoding` (400), a transfer coding other than `chunked` (501), a folded header line, a missing or repeated `Host` (400), and a method it does not know (501). A head that takes more than 10 s is closed. | proposed |
| Requests on one connection are served one at a time, in order. | proposed |
| `http.Request` is immutable: `method`, `path`, `query`, `headers`, `client`, `scheme`, `request_id`, and the body, which is read whole within `S-6` (`body()`) or in chunks under a `using` owner (`open_body()`). | proposed |
| `http.Response` is a status, headers and a body: bytes within `S-7`, or a stream written under a `using` owner with backpressure. `http.json(status, value, codec)` is the usual constructor. | proposed |
| `http.Router`: `get`, `post`, `put`, `patch`, `delete` each take a path pattern (`/parts/{id}`; segments only) and a handler `func(http.Request, http.Params) -> Result<http.Response>`. `mount` adds a generated adapter's routes. Two routes with one method and equal patterns are an error of the startup check. | proposed |
| Matching is exact on method and segment count; a literal segment beats a parameter. No match is 404; a path that matches under another method is 405 with `Allow`. `HEAD` is answered from the `GET` route without its body. | proposed |
| Middleware is `func(http.Request, http.Next) -> Result<http.Response>`. `router.use` middlewares run in the order they were added, then those of the route, then the handler; responses return through them in reverse. | proposed |
| A handler's `Err`, and a middleware's, becomes 500 with the framework envelope and no detail; the error is logged with the request identifier. A handler never sees another request's data. | proposed |
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
| `PendingOperation` keeps `attempt`, `expire` and `deadline`. Its descriptor fields become one wait source with four forms: none (polled at each check), a POSIX descriptor, a Windows handle, and *external*, signalled by the embedding loop. `src/nvm/driver.cpp` waits with `poll`, with `WaitForMultipleObjects`, or by returning to the browser. This is the only change to the interface. | proposed |
| Completions are applied in the order they become ready, and by request sequence among those ready at one check, as the driver does today. | proposed |
| Every request and completion is in the trace. Under replay no provider is constructed and none of its code runs. | proposed |
| Error codes extend the existing table ([Stdlib](../lang/Stdlib.md#45-host-services)): 16 `TooLarge`, 17 `Busy`, 18 `OutcomeUnknown`, 19 `PoolExhausted`, 20 `VersionMismatch`, 21 `ConstraintViolation`, 22 `Serialization`. The library maps them to new `IoErrorKind` values of the same names. | proposed |
| Capability groups are added to `nvm::Capabilities`, all denied by default: `ui`, `http_client`, `bridge`, `http_server`, `sql`, `app`, `desktop`. Which target kind may list which is the table of [the overview](Apps030.md#capability-groups). A target's manifest lists the groups and their scope (origins, pool names, operations); the host grants exactly those. A denied service binds and answers 14, as today. | proposed |
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
| The host terminates TLS with OpenSSL 3.5, linked statically into the server target component. TLS 1.2 and 1.3 with the library's default cipher list; nothing older. | proposed |
| Certificate and key are files named in the host's configuration, read at start and on `SIGHUP`. A missing or mismatched pair fails the start. | proposed |
| An outgoing `https` fetch verifies the peer against the system trust store, or a bundle the configuration names. Verification cannot be turned off from Neura code. | proposed |
| Plain HTTP and HTTPS can listen together, each on its own address. | proposed |

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
| The host answers `/livez` (the process is running) and `/readyz` (the startup check passed, pools are connected, it is not draining) itself, on an admin listener bound to loopback by default. `/metrics` on the same listener gives the counters of `neura.app.metric` and the host's own in the Prometheus text format. | proposed |
| Logs are JSON lines on standard error: time, level, request identifier, `traceparent` if the request carried one, message, and named fields, within `S-13`. | proposed |
| On `SIGTERM`, `SIGINT`, or a console close event, the host stops accepting, answers 503 to waiting requests, cancels no worker yet, and waits up to `S-12` for workers to finish; then it cancels the rest, waits the grace period, kills what remains, reports each, and exits. `context.on_shutdown` callbacks do not exist: a request sees only its own cancellation. | proposed |
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
