# `Std.Net.TcpListener`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.TcpListener`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tcp-listener.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.TcpListener`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

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
