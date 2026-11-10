# `Std.Net.TcpStream`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.TcpStream`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.TcpStream`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TcpStream** · [`export let TcpStream = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L46)
  Owns a connected TCP socket with an independently configured read timeout.
- **TcpStream.TcpStream** · [`export let init = func(opened: socket.Handle)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L55)
  Use connect or TcpListener.accept; the handle comes from the library.
- **TcpStream.TcpStream** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L61)
  Up to `max` bytes; an empty list once the peer has closed its side. Waits at most the read timeout, then fails with TimedOut. A maximum below 1 traps.
- **TcpStream.TcpStream** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L75)
  Writes every byte, repeating partial host writes. When the task's scope is cancelled while the write waits, it fails with Cancelled and the message "stream write cancelled after N bytes", where N is the number of bytes already sent.
- **TcpStream.TcpStream** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L107)
  The longest a read waits; zero waits without limit. A negative timeout traps with "invalid argument: negative timeout".
- **TcpStream.TcpStream** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L113)
  Releases the connection. Closing again does nothing and returns Ok; the stream is closed even when the host reports an error.
- **TcpStream** · [`export let connect = func(host: string, port: int, timeout: Duration) -> Result<TcpStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L130)
  Connects to `host` (a name or an address) and `port`, waiting at most `timeout` (zero waits without limit). A port outside 0 through 65,535 traps with "invalid argument: port"; a negative timeout traps with "invalid argument: negative timeout".
- **TcpStream** · [`export let with_connection = func<T>(host: string, port: int, body: func(TcpStream) -> Result<T>) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L152)
  Connects to `host` and `port` without a time limit, runs `body` with the stream, and closes the stream exactly once however the body ends (Concurrency.md section 7, "Resources"). - If the connection fails, the open error is returned and `body` is not called. - `body` runs in a scope the helper owns, so every task started in that scope has finished before the stream is closed. - The stream is closed after the body returns a value, after it returns an error, and after it returns because its scope was cancelled. - A body `Ok` whose close fails returns the close error. A body `Err` whose close fails returns a `Context` error whose cause is the body's error and whose message names the close error's message. A trap in the body ends the run without closing; the runner then closes every socket the run left open.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- There is no scoped helper ([section 5.1](Stdlib-Design#51-failure-model)).
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

### 11.4 `Std.Net`

Blocking TCP only. Needs P2 and T1.

- `TcpStream`: the module function `connect(host: string, port: int, timeout: Duration) → Result<TcpStream>`. Members `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `set_read_timeout(timeout: Duration)` (zero for none; an expired timeout fails with kind `TimedOut`), and `close()`.
- `TcpListener`: the module function `bind(host: string, port: int) → Result<TcpListener>`. Members `accept() → Result<TcpStream>`, `local_port() → int`, and `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 traps.
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

UDP, TLS, HTTP, and name-resolution APIs are not in the first version.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.
