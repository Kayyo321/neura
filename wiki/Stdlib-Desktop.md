# `Std.Desktop`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Desktop`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Desktop`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Desktop** · [`export const MaxWindows: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L34)
  The most windows open at once (D-1). `open_window` answers Busy beyond it. The window host refuses a package that raises D-1 or D-3, because this child keeps the defaults.
- **Desktop** · [`export let Registry = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L45)
  The operation identifiers a desktop target's bridge serves, as `registry.json` lists them. An identifier has 1 to 128 bytes of lowercase ASCII letters, digits, `.`, `_` and `-`. The canonical text is `{"operations":[...],"schema":1}` with the identifiers in ascending byte order and no spaces; the host and the child compare the SHA-256 of exactly that text. Tagged `#Final`.
- **Desktop.Registry** · [`export last operations: string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L47)
  The identifiers in ascending byte order.
- **Desktop.Registry** · [`export let init = func(ids: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L51)
  A registry of `ids`, which must be valid, distinct, and in ascending byte order. Traps with "invalid argument: registry operation" otherwise.
- **Desktop.Registry** · [`export let contains = func(id: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L64)
  Whether `id` is listed.
- **Desktop.Registry** · [`export let json = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L74)
  The canonical text of `registry.json`.
- **Desktop.Registry** · [`export let digest = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L86)
  The 32-byte SHA-256 of the canonical text.
- **Desktop.Registry** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L88)
- **Desktop.Registry** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L90)
- **Desktop.Registry** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L97)
- **Desktop** · [`export let Window = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L102)
  An open window of this application, identified by the host's handle. Tagged `#Final`.
- **Desktop.Window** · [`export last handle: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L104)
  The host's handle; never reused while the application runs.
- **Desktop.Window** · [`export let init = func(window_handle: uint32, context: Context)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L109)
  Use `Context.open_window`.
- **Desktop.Window** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L115)
  Closes the window. Its renderer and state end; a call in flight from it is cancelled.
- **Desktop.Window** · [`export let focus = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L118)
  Gives the window the input focus.
- **Desktop.Window** · [`export let set_title = func(title: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L121)
  Replaces the window's title.
- **Desktop.Window** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L124)
- **Desktop.Window** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L126)
- **Desktop.Window** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L133)
- **Desktop** · [`export let Event = enum ( Closed(Window), Focused(Window), Blurred(Window), Resized(Window, int, int), MenuActivated(string, Window) )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L137)
  A window event in the order the host observed it.
- **Desktop** · [`export let NotifyOutcome = enum (Shown, Denied, Unsupported)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L146)
  What became of a notification.
- **Desktop** · [`export let Call = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L150)
  One bridge call from a window. It carries nothing of the page: no URL, no script, no path. Tagged `#Final`.
- **Desktop.Call** · [`export last id: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L152)
  The host's request identifier.
- **Desktop.Call** · [`export last window: Window`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L154)
  The window whose main frame made the call.
- **Desktop.Call** · [`export last operation: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L156)
  The registry operation identifier.
- **Desktop.Call** · [`export last request: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L158)
  The request: the JSON bytes of the contract operation.
- **Desktop.Call** · [`export last deadline_nanos: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L160)
  Nanoseconds the caller still waited when the host forwarded the call (at most D-4).
- **Desktop.Call** · [`export let init = func(call_id: uint32, from: Window, operation_id: string, body: uint8[list], nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L163)
  Built by the context from a Call frame.
- **Desktop.Call** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L171)
- **Desktop** · [`export let Dispatcher = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L195)
  Maps each operation of a registry to its implementation. An operation without one answers the caller with error 13 (Unsupported). Tagged `#Final`.
- **Desktop.Dispatcher** · [`export last registry: Registry`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L197)
  The operations this dispatcher serves.
- **Desktop.Dispatcher** · [`export let init = func(served: Registry)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L202)
  A dispatcher for `served`, with no implementation yet.
- **Desktop.Dispatcher** · [`export let add = func(operation: string, handler: func(Call) -> Result<uint8[list]>) -> Dispatcher`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L208)
  Adds the implementation of `operation`, which must be in the registry and not yet added. Traps with "invalid argument: dispatcher operation" otherwise.
- **Desktop.Dispatcher** · [`export let dispatch = func(call: Call) -> [uint32, uint8[list]]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L217)
  The status and reply bytes of `call`: 0 and the reply, or an error code and the UTF-8 message.
- **Desktop.Dispatcher** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L238)
- **Desktop** · [`export let Context = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L244)
  The connection of the native child to its window host. Tagged `#Final`, `#NotThreadSafe`.
- **Desktop.Context** · [`export last registry: Registry`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L246)
  The registry sent in Hello.
- **Desktop.Context** · [`export let init = func(served: Registry)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L268)
  Use `run_application`.
- **Desktop.Context** · [`export let open_window = func(title: string, width: int, height: int) -> Result<Window>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L274)
  Opens a window of `width` by `height` pixels that shows the bundle's start route. Busy when D-1 windows are open.
- **Desktop.Context** · [`export let open_window_at = func(title: string, width: int, height: int, min_width: int, min_height: int, route: string) -> Result<Window>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L279)
  Opens a window with a minimum size and the bundle route it starts on. Sizes are 0 to 65,535; a route starts with `/`. Busy when D-1 windows are open.
- **Desktop.Context** · [`export let open_windows = func -> Window[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L314)
  The windows open now, in the order they opened.
- **Desktop.Context** · [`export let data_directory = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L317)
  The application's own data directory (`$XDG_DATA_HOME/<id>` on Linux).
- **Desktop.Context** · [`export let notify = func(title: string, body: string) -> Result<NotifyOutcome>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L327)
  Shows a system notification: Shown, Denied (notifications turned off), or Unsupported (no notification service).
- **Desktop.Context** · [`export let clipboard_write = func(text: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L355)
  Replaces the clipboard's text. CapabilityDenied unless the package grants `desktop.clipboard_write`.
- **Desktop.Context** · [`export let clipboard_read = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L370)
  The clipboard's text. CapabilityDenied unless the package grants `desktop.clipboard_read`.
- **Desktop.Context** · [`export let next_event = func -> Result<Event>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L379)
  The next window event, waiting for one. UnexpectedEnd once the host has closed the channel.
- **Desktop.Context** · [`export let run = func(dispatcher: Dispatcher) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L394)
  Answers bridge calls with `dispatcher` and consumes events until the host ends the channel: after the last window closes, after `quit`, or when the host itself ends. A Closed event forgets its window; every other event is ignored. A dispatcher for another registry is InvalidInput.
- **Desktop.Context** · [`export let quit = func(status: int) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L425)
  Ends the application with `status` (0 to 255). The host closes every window and the channel; `quit` returns when the channel has ended, `run` then returns, and `run_application` answers `status`. Calls and events still in flight are dropped.
- **Desktop.Context** · [`export let exit_status = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L447)
  The process status the application ends with.
- **Desktop.Context** · [`export let connect = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L450)
  Sends the Hello frame and waits for the host's. Used once by `run_application`.
- **Desktop.Context** · [`export let window_request = func(service: string, handle: uint32, title: string, with_title: bool) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L480)
  Runs a window request that answers nothing.
- **Desktop.Context** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L685)
- **Desktop** · [`export let run_application = func(registry: Registry, start: func(Context) -> Result<Unit>) -> program.ExitCode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L692)
  Connects to the window host, runs `start`, and answers the process status: the status given to `quit`, 0 when `start` returns Ok, and 1 when it returns an error or the handshake fails. The error goes to standard error.
<!-- END SOURCE API -->

## Detailed contracts

The following section is the module's part of the standard-library design. The source and current [language status](Status) identify shipped behavior.

### 11.5 `Std.Desktop`

*Since 0.3.0; Linux x86-64 qualified under Xvfb.* The native child of a desktop application ([Desktop design](https://github.com/Kayyo321/neura/blob/main/docs/design/Desktop030.md)). The child speaks to its window host over standard input and output with the framed protocol of the design, so a desktop child writes its log to standard error and never to standard output. It needs no capability group: the window host performs windows, notifications and the clipboard, and `neura build --native --deny-all` is the intended build of a child.

- **`Registry(operations: string[list])`** lists the bridge operations, each 1 to 128 bytes of `a`–`z`, `0`–`9`, `.`, `_` and `-`, distinct and in ascending byte order (otherwise it traps with `invalid argument: registry operation ...`). `json()` is the canonical text of `registry.json`, `{"operations":[...],"schema":1}`, and `digest()` its SHA-256. The child sends this digest in its Hello; a host whose `registry.json` hashes differently ends the application before any operation runs.
- **`Dispatcher(registry)`** with `add(operation, handler: func(Call) -> Result<uint8[list]>)` maps operations to implementations; adding an operation outside the registry, or twice, traps. A `Call` carries `id`, `window`, `operation`, `request` (the JSON bytes of the contract operation) and `deadline_nanos`, and nothing of the page.
- **`run_application(registry, start: func(Context) -> Result<Unit>) -> ExitCode`** connects, calls `start`, and answers 0, the status given to `quit`, or 1 when the handshake or `start` fails.
- **`Context`**: `open_window(title, width, height)` and `open_window_at(title, width, height, min_width, min_height, route)` answer `Result<Window>` (Busy past the 8 windows of D-1; the child keeps the default D-1 and D-3, and the window host refuses a package that raises them); `open_windows()`; `next_event() -> Result<Event>` with `Event` = `Closed(Window)`, `Focused(Window)`, `Blurred(Window)`, `Resized(Window, int, int)`, `MenuActivated(string, Window)`; `run(dispatcher)` answers bridge calls one at a time, in the order the host sent them, until the host ends the channel; `quit(status)`; `data_directory()`; `notify(title, body) -> Result<NotifyOutcome>` (`Shown`, `Denied`, `Unsupported`); `clipboard_write(text)` and `clipboard_read()`, which answer `CapabilityDenied` unless the package grants `desktop.clipboard_write` or `desktop.clipboard_read`.
- **`Window`**: `handle`, `close()`, `focus()`, `set_title(title)`.
- A handler's `Err` reaches the page as status 100 with the error's message; an operation without an implementation as status 13; a reply larger than D-3 as status 16 with no body. A trap in the child ends the application.

The design's named-argument form `open_window(title:, width:, ...)` waits for named arguments; the entry form `start: func(desktop.Context)` called by a desktop target waits for project targets ([Projects](https://github.com/Kayyo321/neura/blob/main/docs/design/Projects030.md)). Until then a program's `main` returns `run_application(registry, start)`.
