# `Std.Net.TcpStream`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.TcpStream`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.TcpStream`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TcpStream** · [`export let TcpStream = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L48)
  Owns a connected TCP socket with an independently configured read timeout. Tagged `#Final`, `#NoDiscard(reason: "a stream must be closed")`, `#NotThreadSafe`.
- **TcpStream.TcpStream** · [`export let read_timeout: Duration = of_nanos(0) { get; set }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L59)
  The longest a read waits; zero, the initial value, waits without limit. Setting a negative timeout traps with "invalid argument: negative timeout". Since 0.2.0.
- **TcpStream.TcpStream** · [`export let init = func(opened: socket.Handle)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L67)
  Use connect or TcpListener.accept; the handle comes from the library.
- **TcpStream.TcpStream** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L74)
  Up to `max` bytes; an empty list once the peer has closed its side. Waits at most the read timeout, then fails with TimedOut. A maximum below 1 traps. Tagged `#NoDiscard`.
- **TcpStream.TcpStream** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L89)
  Writes every byte, repeating partial host writes. When the task's scope is cancelled while the write waits, it fails with Cancelled and the message "stream write cancelled after N bytes", where N is the number of bytes already sent. Tagged `#NoDiscard`.
- **TcpStream.TcpStream** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L121)
  Releases the connection. Closing again does nothing and returns Ok; the stream is closed even when the host reports an error.
- **TcpStream** · [`export let connect = func(host: string, port: int, timeout: Duration) -> Result<TcpStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L139)
  Connects to `host` (a name or an address) and `port`, waiting at most `timeout` (zero waits without limit). A port outside 0 through 65,535 traps with "invalid argument: port"; a negative timeout traps with "invalid argument: negative timeout". Tagged `#NoDiscard`.
- **TcpStream** · [`export let with_connection = func<T>(host: string, port: int, body: func(TcpStream) -> Result<T>) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-stream.nra#L162)
  Connects to `host` and `port` without a time limit, runs `body` with the stream, and closes the stream exactly once however the body ends (Concurrency.md section 7, "Resources"). - If the connection fails, the open error is returned and `body` is not called. - `body` runs in a scope the helper owns, so every task started in that scope has finished before the stream is closed. - The stream is closed after the body returns a value, after it returns an error, and after it returns because its scope was cancelled. - A body `Ok` whose close fails returns the close error. A body `Err` whose close fails returns a `Context` error whose cause is the body's error and whose message names the close error's message. A trap in the body ends the run without closing; the runner then closes every socket the run left open. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Results that must be used.** The functions and methods of `Std.Io`, `Std.Fs`, and `Std.Net` whose `Result` can report a failed read, write, open, connection, or file system operation are tagged `#NoDiscard`, and so are the classes `File`, `TcpStream`, and `TcpListener`: a statement that drops such a result warns with `W0003` ([Tags](Tags#nodiscard)), and `let _ = ...;` drops one on purpose. `close()` is not tagged.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- **Scoped helpers.** `File.with_open<T>(path, mode, body: (File) → Result<T>) → Result<T>` and `TcpStream.with_connection<T>(host, port, body: (TcpStream) → Result<T>) → Result<T>` open the resource, run the body, and close the resource exactly once on every ordinary completion: a returned value, a returned error, and a return after the body's scope was cancelled. With tasks a body can end early by cancellation, and the close must still happen ([Concurrency](Concurrency#7-cancellation-deadlines-and-resources)).
  - If the open fails, the helper returns that error and does not call the body.
  - The body runs in a `scope` the helper owns, so every task started in that scope has finished before the resource is closed.
  - If the close succeeds, the helper returns the body's result unchanged. If the close fails after a body `Ok`, it returns the close error. If the close fails after a body `Err`, it returns a `Context` error whose cause is the body's error and whose message carries the close error's message: `closing the resource also failed: ` and that message from `with_open`, `close failed after the body failed: ` and that message from `with_connection`.
  - A file is closed even when the body closed it already, which reports no error. `with_connection` connects without a time limit.
  - A trap in the body ends the run without cleanup; the runner then closes what the run left open.
- **`Closeable.release<T>(resource: Closeable, outcome: Result<T>) → Result<T>`** is the closing rule on its own: it closes the resource and combines the result with the outcome of the work, as `with_open` does. A program that owns another kind of closeable resource calls it exactly once, after the work, on every ordinary completion.
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

### 11.4 `Std.Net`

TCP only. A call waits for its result; in a program with tasks the waiting task parks and the others run, and cancelling its scope ends the wait with kind `Cancelled` ([Concurrency](Concurrency#104-host-io)). Needs P2 and T1.

- `TcpStream`: the module functions `connect(host: string, port: int, timeout: Duration) → Result<TcpStream>` and `with_connection<T>(host, port, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). Members `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), the field `read_timeout: Duration` (zero, the initial value, for none; an expired timeout fails with kind `TimedOut`; assigning a negative duration traps with `invalid argument: negative timeout`), and `close()`. A write that is cancelled while it waits fails with kind `Cancelled` and the message `stream write cancelled after N bytes`, where N is the number of bytes already sent.
- `TcpListener`: the module function `bind(host: string, port: int) → Result<TcpListener>`. Members `accept() → Result<TcpStream>`, the read-only field `local_port: int`, and `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 traps.
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

The library has no UDP, TLS, HTTP, or name-resolution APIs.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.
