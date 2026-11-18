# SQL

`Std.Data.Sql` reaches SQLite and PostgreSQL through connection pools that live in the host. Under [`neura-server-host`](ServerHost.md) the host owns every connection: a request's worker holds only numbered leases, and when the request ends the host rolls back whatever the request left open and returns its connections to their pools. `neura run` and `neura test` serve the same calls in process with `--sql-pool`, so repository code is tested the way it runs. The contract is the SQL section of [the backend design note](../design/Backend030.md#sql); this page describes what 0.3.0 does.

## A repository

<!-- example: check -->
```neura
let sql = import:Std.Data.Sql;
let.promote _ = import:Std.Core.Result;

let Parts = module (
    /// @brief The quantity of the part with `code`, or an error of kind NotFound.
    export let quantity_of = func(pool: sql.Pool, code: string) -> Result<int64> {
        return match (pool.query_one("SELECT quantity FROM parts WHERE code = $1", code)) {
            Result.Ok(row) => row.int64("quantity"),
            Result.Err(problem) => Result<int64>.Err(problem),
        };
    };
);
```

A server image gets its pool from `context.sql_pool("main")`, which fails when the host has no pool of that name, so a missing pool fails the [startup check](ServerHost.md#running-the-host). `examples/server/parts-sql.nra` is a complete create, read, update and delete backend; the same code runs against SQLite and PostgreSQL.

## Configuring pools

`neura-server-host --sql-pool <name>=<url>` declares a pool; its other settings are configuration keys `sql.<name>.<setting>`, given with `--config` or in the `--config-file`. These keys and every password stay in the host: they are removed from the configuration before any worker starts, and `Context.config` answers `None` for any key that starts with `sql.`, even when an environment variable under the configuration prefix names it.

| URL | Database |
| --- | --- |
| `sqlite:<path>` | An SQLite file, created when missing. A relative path is relative to the host's working directory |
| `sqlite::memory:` | A private in-memory database; its pool has one connection |
| `postgresql://user@host:port/database?options` or `postgres://...`, or a libpq `key=value` string | PostgreSQL, in builds with the PostgreSQL provider. The URL must not hold a password |

| Setting | Meaning | Default |
| --- | --- | --- |
| `sql.<name>.url` | The URL, instead of `--sql-pool` | |
| `sql.<name>.password` | The PostgreSQL password, in the host's configuration only | |
| `sql.<name>.password_env` | The environment variable that holds the password; the start fails when it is not set | |
| `sql.<name>.max_connections` | S-9, connections open at once (1 to 1,024) | 8 |
| `sql.<name>.max_waiting` | S-10, acquires waiting at once; one more fails with `PoolExhausted` | 64 |
| `sql.<name>.acquire_timeout` | S-10, seconds an acquire waits before `TimedOut` (1 to 600) | 5 |
| `sql.<name>.page_rows` | S-11, the most rows one page may ask for; more is `TooLarge` | 1,000 |
| `sql.<name>.migrations` | The migration directory; a relative path is relative to the directory of the last `--config-file`, or to the working directory | |
| `sql.<name>.migrate` | `manual` or `on_start` | `manual` |

The host checks every setting before it starts and lists every problem: an unknown setting, a pool with settings and no URL, a URL it cannot parse, a password in a URL, an SQLite pool with a password. It then opens one connection of each pool and checks its migrations; a pool that does not connect fails the start.

PostgreSQL connects with `sslmode=verify-full` unless the URL names another mode, which the host accepts only when every host of the URL is a loopback address (`localhost`, `127.0.0.1`, `::1`) or a Unix socket directory. Certificates are checked against libpq's default root file or the `sslrootcert` the URL names.

`neura run` and `neura test` take `--sql-pool <name>=<url>`, `--sql-pool <name>.migrations=<directory>` (applied before the program starts) and `--sql-pool <name>.password_env=<variable>`, and grant the `sql` group with them. Their pools have no deadline.

## Connections, transactions and cursors

| `Std.Data.Sql` | Meaning |
| --- | --- |
| `pool(name)`, `Context.sql_pool(name)` | The pool, or `NotFound`; `CapabilityDenied` without a host or `--sql-pool` |
| `Pool.acquire()` | A leased `Connection`, waiting at most S-10 |
| `Pool.begin()` | A `Transaction` on a connection of its own; closing it returns the connection |
| `Pool.execute`, `query_one`, `query_page` | One statement on a connection that is acquired and returned around it |
| `Connection.execute(sql, parameters...)` | The affected row count; 0 for a query |
| `Connection.query_one(sql, parameters...)` | The first row, or `NotFound` |
| `Connection.query_page(sql, limit, parameters...)` | At most `limit` rows as an `ImmutableList<Row>`; a limit of 0 runs the statement and returns none; a limit above S-11 is `TooLarge` |
| `Connection.open_cursor(sql, parameters...)` | A `Cursor` whose `fetch(max)` returns the next rows; `has_more()`, `close()` |
| `Connection.begin()` | A `Transaction` on this connection |
| `execute_values`, `query_page_values` | The same with a `Value[list]`, for a parameter list built at run time |
| `Transaction.commit()`, `rollback()`, `close()` | `close` without a commit rolls back; after a commit, `close` only releases |
| `in_transaction(pool, body)` | Runs `body` in a transaction: commits after `Ok`, rolls back after `Err`, and returns the connection |

`Connection`, `Transaction` and `Cursor` are closeable. A closed one, a transaction after its end, and a cursor after its close fail with `InvalidInput`; closing twice does nothing. A connection runs one statement at a time; while a cursor is open on it, other statements fail with `InvalidInput`. A second `begin` on a connection with an open transaction is `InvalidInput`.

A commit that fails has rolled back, with one exception: when the connection fails after `COMMIT` was sent and before its answer, `commit` fails with `OutcomeUnknown`, nothing is retried, and the host discards that connection. On PostgreSQL a statement that fails inside a transaction leaves it failed: later statements fail with `InvalidInput` and `commit` reports `InvalidInput` after rolling back. On SQLite the transaction stays usable and the failed statement has no effect.

SQLite transactions begin with `BEGIN IMMEDIATE`, which takes the write lock at once. A transaction therefore never fails later on upgrading a read lock, and writers queue on the busy timeout instead.

## Parameters and values

SQL is a string and values are parameters, written `$1`, `$2`, ... on both databases; text inside quotes is not a placeholder. The statement methods take the parameters as trailing arguments: a string is `Text`, a signed integer `Int`, an unsigned integer `Int` when it is at most 2^63 - 1 and otherwise `InvalidInput`, each float width `Float` (widened exactly), a `bool` `Bool`, and a `Value` object itself. `null_value()` and `bytes_value(list)` make the values a literal cannot write. An integer literal written directly as an argument is an `int32`; give it a type, such as a `last limit: int64 = ...`, when it needs more.

On SQLite every placeholder must have the form `$n`, every `$n` must have a parameter, and every parameter must be used; `?`, `:name` and `@name` are `InvalidInput`. A call holds one statement: text after it other than white space and comments is `InvalidInput`. A migration may hold several.

`Row` is immutable. Its accessors take a column name (an exact match first, then one that differs in ASCII case only) and return a `Result`:

| Accessor | Accepts | Otherwise |
| --- | --- | --- |
| `int64`, `optional_int64` | `Int` | `InvalidInput` |
| `int32`, `optional_int32` | `Int` within the `int32` range | `InvalidInput` naming the value, never a wrapped number |
| `bool`, `optional_bool` | `Bool`; `Int` 0 or 1, which SQLite stores | `InvalidInput` |
| `float64`, `optional_float64` | `Float`; `Int` within ±2^53, which a `float64` holds exactly | `InvalidInput` |
| `text`, `optional_text` | `Text` | `InvalidInput` |
| `bytes`, `optional_bytes` | `Bytes`, copied | `InvalidInput` |
| `value(name)`, `at(index)` | Any | `NotFound` for a missing column |

A missing column is `NotFound`; `NULL` read by a required accessor is `InvalidInput`, and by an `optional_` accessor `None`.

| Database type | Value |
| --- | --- |
| SQLite `INTEGER`, `REAL`, `TEXT`, `BLOB`, `NULL` | `Int`, `Float`, `Text` (`Bytes` when the stored text is not UTF-8), `Bytes`, `Null` |
| PostgreSQL `bool` | `Bool` |
| `int2`, `int4`, `int8` | `Int` |
| `float4`, `float8` | `Float`; `NaN` and the infinities included |
| `text`, `varchar`, `bpchar` | `Text` |
| `bytea` | `Bytes` |
| `uuid`, `timestamptz`, `numeric` | `Text`, in PostgreSQL's output form |
| Any other type, such as `jsonb`, `date` or an array | The statement fails with `Unsupported` naming the column and the type; cast it in the query, for example `document::text` |

PostgreSQL parameters are sent with their types: `Int` as `int8`, `Float` as `float8`, `Bool` as `bool`, `Bytes` as `bytea`, and `Text` untyped so that the server infers the column's type, which makes text usable for `uuid`, `timestamptz` and `numeric` columns.

## Errors

Every failure is an `Error.Custom` whose detail is an `SqlError`: its `kind`, the `action` that failed, and the database's `detail` (a constraint, column or type name, or its message; never row values). `kind_of(error)` reads the kind, through `Context` errors too, and `code()` is `sql.` and the kind in lower case with underscores, such as `sql.constraint_violation`.

| Kind | When |
| --- | --- |
| `NotFound` | No such pool; `query_one` found no row; no such column |
| `InvalidInput` | A malformed statement or placeholder, a closed or forged handle, a statement while a cursor is open, a `NULL` or a value that does not convert, a failed PostgreSQL transaction |
| `PermissionDenied` | The database refused the access |
| `ConnectionFailed` | A connection could not be opened |
| `ConnectionLost` | The connection failed during a statement |
| `TimedOut` | The request's deadline interrupted the statement, or an acquire waited S-10 |
| `Unsupported` | A column type that does not convert; a PostgreSQL pool in a build without the provider |
| `CapabilityDenied` | No host pools: `neura run` without `--sql-pool`, or a host without pools |
| `TooLarge` | A page above S-11 rows, or a page or a statement above the channel's 1 MiB |
| `Busy` | SQLite's lock is held past the busy timeout; PostgreSQL is out of resources |
| `OutcomeUnknown` | The connection failed between `COMMIT` and its answer |
| `PoolExhausted` | S-10 acquires already wait on the pool |
| `ConstraintViolation` | A unique, foreign-key, check or not-null failure; the detail is the constraint's name (PostgreSQL) or SQLite's message, such as `UNIQUE constraint failed: users.email` |
| `Serialization` | A serialization failure or a deadlock (PostgreSQL), or an SQLite snapshot that cannot be upgraded |
| `Cancelled`, `Other` | A cancelled statement; anything else |

Retrying is the application's decision; the library and the host retry nothing.

## Deadlines and leases

Each statement runs on a host thread with the request's deadline. SQLite's busy timeout is the time left until the deadline (60 s under `neura run` and `neura test`, which have no deadline), and a progress handler interrupts a running statement when it passes. PostgreSQL statements are sent with libpq in non-blocking mode; at the deadline the host sends `PQcancel` (`PQcancelBlocking`) and reads what the server still sends. Either way the call fails with `TimedOut` and the host answers the request with 504.

The host keeps a session per request. When the request ends, by a response, an error, a trap or the deadline, the host closes the request's cursors, rolls back its open transactions, resets each connection (`DISCARD ALL` on PostgreSQL; on SQLite, no statement left and no transaction) and returns it to its pool. A connection whose state is unknown, or one that failed after `COMMIT`, is closed instead. Calls a program makes after its response has been sent fail with `ConnectionLost`, because the request's channel is closed by then.

A handle is a number valid in one execution; a number the session did not hand out, or one already released, is `InvalidInput`.

## Migrations

A pool's migrations are files `NNNN_name.sql` in the directory `sql.<name>.migrations` names, numbered from `0001` without gaps, each with one or more statements; `name` is letters, digits and `_`. Other files are ignored. A gap, a repeated number, a misnamed `.sql` file or an empty file is reported as `error[E1044]: invalid migration set` and fails the start.

The host records applied migrations in `neura_migrations(version, name, checksum, applied_at)`, with the SHA-256 of each file and the UTC time. Each migration is applied in its own transaction with its record, in order, forward only; a migration that fails is rolled back and nothing after it runs. A migration must not hold its own `BEGIN` or `COMMIT`.

| Mode | At start |
| --- | --- |
| `manual` (the default) | The host refuses to start while any migration is pending and names them |
| `on_start` | The host applies the pending ones before the startup check |

`neura-server-host migrate [options] <image.nvr>` applies them and exits without listening. An applied migration whose file has changed, or an applied version the directory lacks, refuses the start and names the file; nothing is repaired or re-run. Hosts that start together serialize on a PostgreSQL advisory lock or on SQLite's write lock; the second finds nothing pending.

## Record and replay

The SQL services are host services like any other: with `--record-dir` each request's statements and their answers are in its trace, and `neura run --replay <trace> <image>` reproduces the request with no host and no database.

## Host services

| Service | Arguments | Success |
| --- | --- | --- |
| `neura.sql.pool` | Pool name | none; `NotFound` (1) without such a pool |
| `neura.sql.acquire` | Pool name, wait in nanoseconds (`int64`; 0 or less: the pool's S-10) | Connection handle (`int64`) |
| `neura.sql.release` | Connection handle | none |
| `neura.sql.begin`, `commit`, `rollback` | Connection handle | none |
| `neura.sql.execute` | Connection handle, SQL text, values | Affected rows (`int64`) |
| `neura.sql.query` | As `execute`, and a row limit (`int32`) | Column names, values, a flag: more rows exist |
| `neura.sql.open_cursor` | As `execute` | Cursor handle, column names |
| `neura.sql.fetch` | Cursor handle, maximum rows (`int32`) | Values, a flag: more rows exist |
| `neura.sql.close_cursor` | Cursor handle | none |
| `neura.sql.detail` | none | The detail text of the execution's last failed SQL call |

All are version 1 with a `uint32` error code, in the `sql` capability group. A host call cannot carry a list of tuples, so *values* are four parallel lists: kinds (`uint8[list]`: 0 Null, 1 Bool, 2 Int, 3 Float, 4 Text, 5 Bytes), bits (`uint64[list]`: the Boolean, the `int64` bits, the `float64` bits, or the index into the next list), the Text values (`string[list]`) and the Bytes values (`uint8[list][list]`). A page lists its values row by row. An error's detail travels separately and is read with `neura.sql.detail`, because a host error is a code only. The services do not overlap: a statement holds the execution, and every task in it, until it ends.

A request worker forwards each call to the host as a `Call` frame of operation 2, and the host answers with a `Return` ([message bodies](../design/Backend030.md#message-bodies)). Both frames are limited to the channel's 1 MiB: a larger statement or page fails with `TooLarge` without reaching the database, and the cure is a cursor with smaller pages.

## Building

SQLite 3.54.1 is compiled in by default (`NEURA_ENABLE_SQL`, on). The build takes the amalgamation that [the server component lock](../../packaging/server-components.lock.toml) pins, from `-DNEURA_SQLITE_AMALGAMATION=<sqlite-amalgamation-3540100.zip>` or, when that is not given, by downloading it at configure time; either way its SHA-256 must match the lock. It is compiled with `SQLITE_THREADSAFE=1`, `SQLITE_OMIT_LOAD_EXTENSION` and `SQLITE_DQS=0`, and every connection runs in WAL mode with `foreign_keys` on.

The PostgreSQL provider needs `-DNEURA_ENABLE_POSTGRES=ON` with `NEURA_ENABLE_SERVER_TLS`, `-DNEURA_LIBPQ_SOURCE=<postgresql-18.7>` and `-DNEURA_LIBPQ_BUILD=<its configured build tree>`, which holds the static `libpq.a`, `libpgcommon_shlib.a` and `libpgport_shlib.a` built against the pinned OpenSSL. Without it a PostgreSQL pool fails the start with `Unsupported`.

The `server-sql` test runs the example backend with curl and the lease, deadline, migration and replay probes on SQLite. `server-sql-postgres` runs them against PostgreSQL 16, 17 and 18 in Docker, by image digest, with host networking on 127.0.0.1; it is skipped without Docker or the provider.

## Not in 0.3.0

Overlapping and cancellable SQL calls (a statement holds its execution), cancellation of a statement when its client disconnects, the `Cancel` frame, a `using` owner for connections (this release has no `using`), `E1044` from a project build (there is no project build yet; the host reports the same rule), named or positional parameters other than `$n`, and PostgreSQL types beyond the table above. The outcome-unknown path of `commit` is implemented and has no test with a connection cut between `COMMIT` and its answer.
