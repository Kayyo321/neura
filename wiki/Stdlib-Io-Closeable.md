# `Std.Io.Closeable`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Io.Closeable`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/closeable.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Io.Closeable`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Closeable** · [`export let Closeable = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/closeable.nra#L13)
  The contract for a resource that callers release explicitly.
- **Closeable** · [`export let release = func<T>(resource: Closeable, outcome: Result<T>) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/closeable.nra#L25)
  Closes the resource and combines that with the outcome of the work that used it. This is the closing rule of the scoped helpers such as File.with_open (Concurrency.md section 7): when the close succeeds the outcome is returned unchanged; when it fails after an Ok, the close error is returned; when it fails after an Err, the result is a Context error whose cause is the outcome's error and whose message carries the close error's message. Call it exactly once, after the work, on every ordinary completion. Tagged `#NoDiscard`.
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
