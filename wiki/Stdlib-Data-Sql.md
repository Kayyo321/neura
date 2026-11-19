# `Std.Data.Sql`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Data.Sql`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Data.Sql`](#exported-declarations)
- [SQL](Sql): pools, configuration, values, errors, deadlines, leases and migrations
- [Server host](Server-Host)

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Sql** · [`export let ErrorKind = enum ( NotFound, InvalidInput, PermissionDenied, ConnectionFailed, ConnectionLost, TimedOut, Unsupported, CapabilityDenied, Cancelled, TooLarge, Busy, OutcomeUnknown, PoolExhausted, ConstraintViolation, Serialization, Other )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L104)
  What went wrong. Host codes map as docs/runtime/Sql.md lists. The library adds NotFound (no row for `query_one`, no such column) and InvalidInput (a closed connection, transaction or cursor, a NULL or a value that does not convert). Since 0.3.0.
- **Sql** · [`export let SqlError = class:errors.ErrorDetail`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L113)
  A failed SQL operation: its kind, what was attempted, and the database's detail (a constraint, column or type name, or its message; never row data). Tagged `#Final`. Since 0.3.0.
- **Sql.SqlError** · [`export last kind: ErrorKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L115)
  The kind of failure.
- **Sql.SqlError** · [`export last action: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L117)
  What the library was doing, such as "execute".
- **Sql.SqlError** · [`export last detail: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L119)
  The database's or the host's detail; may be empty.
- **Sql.SqlError** · [`export let init = func(error_kind: ErrorKind, action_text: string, detail_text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L122)
  Builds an error; the library makes these.
- **Sql.SqlError** · [`export let.override code = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L131)
  "sql." and the kind in lower case with underscores, such as sql.constraint_violation. Tagged `#Pure`.
- **Sql.SqlError** · [`export let.override message = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L136)
  The kind, the action and the detail, such as "ConstraintViolation: execute: UNIQUE constraint failed: parts.code". Tagged `#Pure`.
- **Sql.SqlError** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L145)
  Equal when kind, action and detail are. Tagged `#Pure`.
- **Sql.SqlError** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L154)
  Combines the hashes of the kind, action and detail. Tagged `#Pure`.
- **Sql.SqlError** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L159)
  The message. Tagged `#Pure`.
- **Sql** · [`export let kind_of = func(problem: errors.Error) -> Optional<ErrorKind>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L164)
  The ErrorKind of an SQL error, looking through Context errors; None for any other error. Tagged `#NoDiscard`.
- **Sql** · [`export let ValueKind = enum (Null, Bool, Int, Float, Text, Bytes)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L264)
  The SQL types a value can have. Since 0.3.0.
- **Sql** · [`export let Value = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L270)
  One SQL value: NULL, a Boolean, a 64-bit integer, a 64-bit float, UTF-8 text or bytes. Values are immutable; build them with `null_value`, `bool_value`, `int_value`, `float_value`, `text_value` and `bytes_value`. Tagged `#Final`. Since 0.3.0.
- **Sql.Value** · [`export last kind: ValueKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L272)
  The type of the value.
- **Sql.Value** · [`export let init = func(value_kind: ValueKind, integer_value: int64, real_value: float64, text: string, data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L283)
  Builds a value from its parts; use the constructor functions of Std.Data.Sql.
- **Sql.Value** · [`export let is_null = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L298)
  Whether the value is NULL. Tagged `#Pure`.
- **Sql.Value** · [`export let as_bool = func -> Optional<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L302)
  The Bool payload, or None for another kind. Tagged `#NoDiscard`.
- **Sql.Value** · [`export let as_int64 = func -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L307)
  The Int payload, or None for another kind. Tagged `#NoDiscard`.
- **Sql.Value** · [`export let as_float64 = func -> Optional<float64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L312)
  The Float payload, or None for another kind. Tagged `#NoDiscard`.
- **Sql.Value** · [`export let as_text = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L317)
  The Text payload, or None for another kind. Tagged `#NoDiscard`.
- **Sql.Value** · [`export let as_bytes = func -> Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L322)
  A copy of the Bytes payload, or None for another kind. Tagged `#NoDiscard`.
- **Sql.Value** · [`export let byte_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L335)
  The number of payload bytes of Bytes; 0 otherwise. Tagged `#Pure`.
- **Sql.Value** · [`export let wire_integer = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L339)
  The raw integer payload: the Int, 1 or 0 for a Bool, 0 otherwise. Tagged `#Pure`.
- **Sql.Value** · [`export let wire_real = func -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L342)
  The raw Float payload; 0.0 for other kinds. Tagged `#Pure`.
- **Sql.Value** · [`export let wire_text = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L345)
  The raw Text payload; empty for other kinds. Tagged `#Pure`.
- **Sql.Value** · [`export let wire_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L349)
  The raw Bytes payload, not copied: the library passes it to the host. Use `as_bytes` for a copy.
- **Sql.Value** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L353)
  Equal kinds and payloads; Floats compare by their bits, so NaN equals NaN. Tagged `#Pure`.
- **Sql.Value** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L373)
  Combines the kind and the payload. Tagged `#Pure`.
- **Sql.Value** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L386)
  NULL, true or false, the number, the text in single quotes with quotes doubled, or the bytes as X'..' hexadecimal. Tagged `#Pure`.
- **Sql** · [`export let null_value = func -> Value`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L398)
  NULL. Tagged `#NoDiscard`.
- **Sql** · [`export let bool_value = func(value: bool) -> Value`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L402)
  A Boolean. Tagged `#NoDiscard`.
- **Sql** · [`export let int_value = func(value: int64) -> Value`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L406)
  A 64-bit integer. Tagged `#NoDiscard`.
- **Sql** · [`export let float_value = func(value: float64) -> Value`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L410)
  A 64-bit float. Tagged `#NoDiscard`.
- **Sql** · [`export let text_value = func(value: string) -> Value`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L414)
  UTF-8 text. Tagged `#NoDiscard`.
- **Sql** · [`export let bytes_value = func(value: uint8[list]) -> Value`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L418)
  Bytes, copied. Tagged `#NoDiscard`.
- **Sql** · [`export let Columns = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L583)
  The column names of a result, shared by its rows. Tagged `#Final`. Since 0.3.0.
- **Sql.Columns** · [`export last names: string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L585)
  The names in result order.
- **Sql.Columns** · [`export let init = func(list: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L588)
  Wraps the host's list.
- **Sql.Columns** · [`export let index_of = func(name: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L595)
  The index of `name`: an exact match first, then one that differs only in ASCII case; -1 when there is none. Tagged `#Pure`.
- **Sql.Columns** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L612)
  Equal names in the same order. Tagged `#Pure`.
- **Sql.Columns** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L629)
  Combines the names' hashes. Tagged `#Pure`.
- **Sql.Columns** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L639)
  The names separated by commas. Tagged `#Pure`.
- **Sql** · [`export let Row = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L666)
  One immutable result row. The typed accessors return Err for a missing column, a NULL in a required read, and a value that does not convert exactly; the `optional_` forms answer None for NULL. No integer is narrowed and no float rounded silently. Tagged `#Final`. Since 0.3.0.
- **Sql.Row** · [`export let init = func(names: Columns, values: Value[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L673)
  Builds a row; results make these.
- **Sql.Row** · [`export let column_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L680)
  The number of columns. Tagged `#Pure`.
- **Sql.Row** · [`export let column_name = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L684)
  The name of column `index`, from 0. Tagged `#Pure`.
- **Sql.Row** · [`export let at = func(index: int) -> Value`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L688)
  The value of column `index`, from 0. Tagged `#Pure`.
- **Sql.Row** · [`export let value = func(name: string) -> Result<Value>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L692)
  The value of the named column. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let int64 = func(name: string) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L702)
  The named column as an int64: an Int. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let optional_int64 = func(name: string) -> Result<Optional<int64>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L706)
  The named column as an int64, None for NULL. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let int32 = func(name: string) -> Result<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L719)
  The named column as an int32: an Int within the int32 range. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let optional_int32 = func(name: string) -> Result<Optional<int32>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L723)
  The named column as an int32, None for NULL. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let bool = func(name: string) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L738)
  The named column as a Boolean: a Bool, or an Int 0 or 1 (SQLite stores Booleans so). Tagged `#NoDiscard`.
- **Sql.Row** · [`export let optional_bool = func(name: string) -> Result<Optional<bool>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L742)
  The named column as a Boolean, None for NULL. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let float64 = func(name: string) -> Result<float64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L758)
  The named column as a float64: a Float, or an Int that a float64 holds exactly. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let optional_float64 = func(name: string) -> Result<Optional<float64>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L763)
  The named column as a float64, None for NULL. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let text = func(name: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L779)
  The named column as text: a Text. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let optional_text = func(name: string) -> Result<Optional<string>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L783)
  The named column as text, None for NULL. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let bytes = func(name: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L796)
  The named column as bytes: a Bytes, copied. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let optional_bytes = func(name: string) -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L801)
  The named column as bytes, None for NULL. Tagged `#NoDiscard`.
- **Sql.Row** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L814)
  Equal column names and values. Tagged `#Pure`.
- **Sql.Row** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L831)
  Combines the names and values. Tagged `#Pure`.
- **Sql.Row** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L841)
  The columns and values, such as "Row(id=7, name='bolt')". Tagged `#Pure`.
- **Sql** · [`export let Pool = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L919)
  A pool the host declares, by name. Pools come from `pool(name)` or `Std.App.Context.sql_pool`; the connections stay in the host. Tagged `#Final`. Since 0.3.0.
- **Sql.Pool** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L921)
  The pool's name in the host's configuration.
- **Sql.Pool** · [`export let init = func(pool_name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L924)
  Names a pool; use `pool(name)`, which checks that the host has it.
- **Sql.Pool** · [`export let acquire = func -> Result<Connection>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L931)
  Leases a connection, waiting at most the pool's S-10 timeout. PoolExhausted when too many acquires already wait, TimedOut after the wait. Tagged `#NoDiscard`.
- **Sql.Pool** · [`export let begin = func -> Result<Transaction>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L941)
  Leases a connection and begins a transaction on it; closing the transaction also returns the connection. Tagged `#NoDiscard`.
- **Sql.Pool** · [`export let execute = func(text: string, parameters: ...) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L950)
  Runs one statement on a connection of its own and returns the affected row count. Tagged `#NoDiscard`.
- **Sql.Pool** · [`export let query_one = func(text: string, parameters: ...) -> Result<Row>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L963)
  The first row of a query on a connection of its own, or NotFound. Tagged `#NoDiscard`.
- **Sql.Pool** · [`export let query_page = func(text: string, limit: int, parameters: ...) -> Result<immutable.ImmutableList<Row>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L977)
  At most `limit` rows of a query on a connection of its own. A limit above the pool's page (S-11) is TooLarge. Tagged `#NoDiscard`.
- **Sql.Pool** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L991)
  The pool's name, such as "Pool(main)"; the host keeps its URL and password. Tagged `#Pure`.
- **Sql.Pool** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L995)
  Equal names. Tagged `#Pure`.
- **Sql.Pool** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1004)
  The name's hash. Tagged `#Pure`.
- **Sql** · [`export let pool = func(name: string) -> Result<Pool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1009)
  The pool named `name`, or NotFound when the host has none; CapabilityDenied without a host. Tagged `#NoDiscard`.
- **Sql** · [`export let Connection = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1052)
  A leased connection. `close` returns it to the pool, rolling back any transaction left open; after close every operation fails with InvalidInput. Statements run one at a time; while a cursor is open on the connection, other statements fail with InvalidInput. Tagged `#Final`, `#NoDiscard(reason: "a connection must be closed")`, `#NotThreadSafe`. Since 0.3.0.
- **Sql.Connection** · [`export last pool_name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1054)
  The pool's name.
- **Sql.Connection** · [`export let init = func(pool_value: string, handle_value: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1061)
  Wraps a lease; use `Pool.acquire`.
- **Sql.Connection** · [`export let handle = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1068)
  The host's lease number, valid in this execution only; the host refuses any other number. Tagged `#Pure`.
- **Sql.Connection** · [`export let is_open = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1072)
  Whether `close` has not been called. Tagged `#Pure`.
- **Sql.Connection** · [`export let execute = func(text: string, parameters: ...) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1076)
  Runs one statement and returns the affected row count (0 for a query). Tagged `#NoDiscard`.
- **Sql.Connection** · [`export let execute_values = func(text: string, values: Value[list]) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1086)
  Runs one statement with a list of parameter values. Tagged `#NoDiscard`.
- **Sql.Connection** · [`export let query_one = func(text: string, parameters: ...) -> Result<Row>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1093)
  The first row of a query, or NotFound. Tagged `#NoDiscard`.
- **Sql.Connection** · [`export let query_page = func(text: string, limit: int, parameters: ...) -> Result<immutable.ImmutableList<Row>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1104)
  At most `limit` rows of a query. A limit of 0 runs it and returns no row; a limit above the pool's page (S-11) is TooLarge. Tagged `#NoDiscard`.
- **Sql.Connection** · [`export let query_page_values = func(text: string, limit: int, values: Value[list]) -> Result<immutable.ImmutableList<Row>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1115)
  At most `limit` rows of a query with a list of parameter values. Tagged `#NoDiscard`.
- **Sql.Connection** · [`export let open_cursor = func(text: string, parameters: ...) -> Result<Cursor>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1123)
  Runs a query whose rows are fetched in pages through the cursor, which must be closed. Tagged `#NoDiscard`.
- **Sql.Connection** · [`export let begin = func -> Result<Transaction>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1134)
  Begins a transaction on this connection; closing the transaction leaves the connection open. A second transaction while one is open is InvalidInput. Tagged `#NoDiscard`.
- **Sql.Connection** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1140)
  Returns the connection to its pool. Closing again does nothing and returns Ok.
- **Sql.Connection** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1151)
  The pool and state, such as "Connection(main, open)"; never the URL or a password. Tagged `#Pure`.
- **Sql** · [`export let Transaction = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1160)
  A transaction. It ends by `commit` or by `close`: `close` without a commit rolls back; after a commit that returned Ok, `close` only releases. A failed commit has rolled back, except OutcomeUnknown: the connection failed after COMMIT was sent, nothing is retried, and the connection is discarded. After the end every statement fails with InvalidInput. Tagged `#Final`, `#NoDiscard(reason: "a transaction must be closed")`, `#NotThreadSafe`. Since 0.3.0.
- **Sql.Transaction** · [`export last pool_name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1164)
  The pool's name.
- **Sql.Transaction** · [`export let init = func(lease_value: int64, pool_value: string, owns: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1173)
  Wraps a begun transaction; use `Pool.begin` or `Connection.begin`.
- **Sql.Transaction** · [`export let is_active = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1181)
  Whether neither commit, rollback nor close has ended it. Tagged `#Pure`.
- **Sql.Transaction** · [`export let execute = func(text: string, parameters: ...) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1185)
  Runs one statement and returns the affected row count. Tagged `#NoDiscard`.
- **Sql.Transaction** · [`export let query_one = func(text: string, parameters: ...) -> Result<Row>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1195)
  The first row of a query, or NotFound. Tagged `#NoDiscard`.
- **Sql.Transaction** · [`export let query_page = func(text: string, limit: int, parameters: ...) -> Result<immutable.ImmutableList<Row>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1205)
  At most `limit` rows of a query. Tagged `#NoDiscard`.
- **Sql.Transaction** · [`export let open_cursor = func(text: string, parameters: ...) -> Result<Cursor>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1216)
  A cursor over a query's rows, within the transaction. Tagged `#NoDiscard`.
- **Sql.Transaction** · [`export let commit = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1227)
  Commits. Err means the transaction rolled back, or for OutcomeUnknown that its outcome is unknown; either way it has ended. Tagged `#NoDiscard`.
- **Sql.Transaction** · [`export let rollback = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1238)
  Rolls back. Tagged `#NoDiscard`.
- **Sql.Transaction** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1249)
  Rolls back unless it was committed or rolled back, and returns the connection when the transaction came from `Pool.begin`. Closing again does nothing and returns Ok.
- **Sql.Transaction** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1276)
  The pool and state, such as "Transaction(main, active)". Tagged `#Pure`.
- **Sql** · [`export let in_transaction = func<T>(on: Pool, body: func(Transaction) -> Result<T>) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1283)
  Runs `body` in a transaction of its own connection: commits when it returns Ok, rolls back when it returns Err, and returns the connection either way. The commit's error replaces an Ok. Tagged `#NoDiscard`.
- **Sql** · [`export let Cursor = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1323)
  The rows of a query read in pages. `close` ends the statement; the connection takes other statements only after it. Tagged `#Final`, `#NoDiscard(reason: "a cursor must be closed")`, `#NotThreadSafe`. Since 0.3.0.
- **Sql.Cursor** · [`export last columns: Columns`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1327)
  The column names.
- **Sql.Cursor** · [`export let init = func(cursor: int64, names: Columns)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1334)
  Wraps an open cursor; use `open_cursor`.
- **Sql.Cursor** · [`export let has_more = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1341)
  Whether rows may remain to fetch. Tagged `#Pure`.
- **Sql.Cursor** · [`export let fetch = func(max: int) -> Result<immutable.ImmutableList<Row>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1346)
  Up to `max` further rows; an empty list at the end. A `max` above the pool's page (S-11) is TooLarge. Tagged `#NoDiscard`.
- **Sql.Cursor** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1365)
  Ends the statement. Closing again does nothing and returns Ok.
- **Sql.Cursor** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/data/sql.nra#L1376)
  The columns and state, such as "Cursor(id, name; open)". Tagged `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The pools live in the host: `neura-server-host` owns the connections its `--sql-pool` options declare, and `neura run` and `neura test` serve the same calls in process with `--sql-pool`. A request holds only leases, which the host rolls back and returns when the request ends.

- SQL is a string and values are parameters, written `$1`, `$2`, ... on both SQLite and PostgreSQL. The statement methods take the parameters as trailing arguments; a `Value` object stands for `NULL` (`null_value()`) and bytes (`bytes_value(list)`).
- `Row` accessors return a `Result`: a missing column is `NotFound`; a `NULL` in a required read, or a value that does not convert exactly, is `InvalidInput`. `int32` of 3,000,000,000 is an error, never a wrapped number.
- A transaction ends by `commit` or by `close`, which rolls back without a commit. `in_transaction(pool, body)` commits after `Ok` and rolls back after `Err`.
- Every failure is an `Error.Custom` holding an `SqlError`; `kind_of` reads its `ErrorKind`, such as `ConstraintViolation` with the constraint's name, `TimedOut` at the request's deadline, or `OutcomeUnknown` when the connection failed after `COMMIT` was sent.
- Calls do not overlap other tasks of the execution.
