# `Std.Io.Closeable`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Io.Closeable`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/closeable.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Io.Closeable`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Closeable** · [`export let Closeable = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/closeable.nra#L12)
  The contract for a resource that callers release explicitly.
- **Closeable** · [`export let release = func<T>(resource: Closeable, outcome: Result<T>) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/closeable.nra#L23)
  Closes the resource and combines that with the outcome of the work that used it. This is the closing rule of the scoped helpers such as File.with_open (Concurrency.md section 7): when the close succeeds the outcome is returned unchanged; when it fails after an Ok, the close error is returned; when it fails after an Err, the result is a Context error whose cause is the outcome's error and whose message carries the close error's message. Call it exactly once, after the work, on every ordinary completion.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- There is no scoped helper ([section 5.1](Stdlib-Design#51-failure-model)).
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.
