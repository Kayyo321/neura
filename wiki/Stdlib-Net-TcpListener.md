# `Std.Net.TcpListener`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.TcpListener`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-listener.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.TcpListener`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TcpListener** · [`export let TcpListener = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-listener.nra#L35)
  Owns a listening socket and retains its port after close.
- **TcpListener.TcpListener** · [`export let init = func(opened: socket.Handle, bound_port: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-listener.nra#L44)
  Use bind; the handle comes from the library.
- **TcpListener.TcpListener** · [`export let accept = func -> Result<TcpStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-listener.nra#L50)
  Waits for the next connection. Fails with Cancelled when the task's scope is cancelled first.
- **TcpListener.TcpListener** · [`export let local_port = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-listener.nra#L62)
  The port the listener is bound to; for a bind to port 0, the one the host chose. Still available after close.
- **TcpListener.TcpListener** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-listener.nra#L66)
  Releases the listener. Repeated calls succeed, and an initial close error still leaves it closed.
- **TcpListener** · [`export let bind = func(host: string, port: int) -> Result<TcpListener>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-listener.nra#L82)
  Listens on `host` (a name or an address; empty for every interface) and `port`; port 0 asks the host for a free port. A port outside 0 through 65,535 traps with "invalid argument: port".
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- **Scoped helpers.** `File.with_open<T>(path, mode, body: (File) → Result<T>) → Result<T>` and `TcpStream.with_connection<T>(host, port, body: (TcpStream) → Result<T>) → Result<T>` open the resource, run the body, and close the resource exactly once on every ordinary completion: a returned value, a returned error, and a return after the body's scope was cancelled. With tasks a body can end early by cancellation, and the close must still happen ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#7-cancellation-deadlines-and-resources)).
  - If the open fails, the helper returns that error and does not call the body.
  - The body runs in a `scope` the helper owns, so every task started in that scope has finished before the resource is closed.
  - If the close succeeds, the helper returns the body's result unchanged. If the close fails after a body `Ok`, it returns the close error. If the close fails after a body `Err`, it returns a `Context` error whose cause is the body's error and whose message carries the close error's message: `closing the resource also failed: ` and that message from `with_open`, `close failed after the body failed: ` and that message from `with_connection`.
  - A file is closed even when the body closed it already, which reports no error. `with_connection` connects without a time limit.
  - A trap in the body ends the run without cleanup; the runner then closes what the run left open.
- **`Closeable.release<T>(resource: Closeable, outcome: Result<T>) → Result<T>`** is the closing rule on its own: it closes the resource and combines the result with the outcome of the work, as `with_open` does. A program that owns another kind of closeable resource calls it exactly once, after the work, on every ordinary completion.
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

### 11.4 `Std.Net`

TCP only. A call waits for its result; in a program with tasks the waiting task parks and the others run, and cancelling its scope ends the wait with kind `Cancelled` ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#104-host-io)). Needs P2 and T1.

- `TcpStream`: the module functions `connect(host: string, port: int, timeout: Duration) → Result<TcpStream>` and `with_connection<T>(host, port, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). Members `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `set_read_timeout(timeout: Duration)` (zero for none; an expired timeout fails with kind `TimedOut`), and `close()`. A write that is cancelled while it waits fails with kind `Cancelled` and the message `stream write cancelled after N bytes`, where N is the number of bytes already sent.
- `TcpListener`: the module function `bind(host: string, port: int) → Result<TcpListener>`. Members `accept() → Result<TcpStream>`, `local_port() → int`, and `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 traps.
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

The library has no UDP, TLS, HTTP, or name-resolution APIs.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.
