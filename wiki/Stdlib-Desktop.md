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

- **Desktop** · [`export const MaxWindows: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L44)
  The most windows open at once (D-1). `open_window` answers Busy beyond it. The window host refuses a package that raises D-1 or D-3, because this child keeps the defaults.
- **Desktop** · [`export let Registry = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L63)
  The operation identifiers a desktop target's bridge serves, as `registry.json` lists them. An identifier has 1 to 128 bytes of lowercase ASCII letters, digits, `.`, `_` and `-`. The canonical text is `{"operations":[...],"schema":1}` with the identifiers in ascending byte order and no spaces; the host and the child compare the SHA-256 of exactly that text. Tagged `#Final`.
- **Desktop.Registry** · [`export last operations: string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L65)
  The identifiers in ascending byte order.
- **Desktop.Registry** · [`export let init = func(ids: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L69)
  A registry of `ids`, which must be valid, distinct, and in ascending byte order. Traps with "invalid argument: registry operation" otherwise.
- **Desktop.Registry** · [`export let contains = func(id: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L82)
  Whether `id` is listed.
- **Desktop.Registry** · [`export let json = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L92)
  The canonical text of `registry.json`.
- **Desktop.Registry** · [`export let digest = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L104)
  The 32-byte SHA-256 of the canonical text.
- **Desktop.Registry** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L106)
- **Desktop.Registry** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L108)
- **Desktop.Registry** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L115)
- **Desktop** · [`export let Host = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L160)
  The services of the window host, usable from any task. Each request waits for its answer while the reader task runs; a request of a cancelled task answers Cancelled. Requests made through one `Host` take turns; `Context.host()` and `Call.host()` give every call its own. Tagged `#Final`, `#Synchronized`.
- **Desktop.Host** · [`export let init = func(frames: Writer, requests: atomics.AtomicInt64, registrations: channels.Sender<Waiter>, status: atomics.AtomicInt64, windows: atomics.AtomicInt64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L173)
  Built by `run_application` and `Context`; shares the connection's handles.
- **Desktop.Host** · [`export let fork = func -> Host`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L183)
  Another Host on the same connection, for another task.
- **Desktop.Host** · [`export let window = func(handle: uint32) -> Window`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L186)
  The window with `handle`, open or not; a request for a closed one answers InvalidInput.
- **Desktop.Host** · [`export let open_window = func(title: string, width: int, height: int) -> Result<Window>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L190)
  Opens a window of `width` by `height` pixels that shows the bundle's start route. Busy when D-1 windows are open.
- **Desktop.Host** · [`export let open_window_at = func(title: string, width: int, height: int, min_width: int, min_height: int, route: string) -> Result<Window>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L195)
  Opens a window with a minimum size and the bundle route it starts on. Sizes are 0 to 65,535; a route starts with `/`. Busy when D-1 windows are open.
- **Desktop.Host** · [`export let data_directory = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L229)
  The application's own data directory (`$XDG_DATA_HOME/<id>` on Linux).
- **Desktop.Host** · [`export let notify = func(title: string, body: string) -> Result<NotifyOutcome>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L239)
  Shows a system notification: Shown, Denied (notifications turned off), or Unsupported (no notification service).
- **Desktop.Host** · [`export let clipboard_write = func(text: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L267)
  Replaces the clipboard's text. CapabilityDenied unless the package grants `desktop.clipboard_write`.
- **Desktop.Host** · [`export let clipboard_read = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L277)
  The clipboard's text. CapabilityDenied unless the package grants `desktop.clipboard_read`.
- **Desktop.Host** · [`export let quit = func(status: int) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L288)
  Ends the application with `status` (0 to 255). The host closes every window and the channel, so the calls in flight are cancelled and the process ends with `status`. Returns once the channel has ended; code after it in a bridge operation may not run.
- **Desktop.Host** · [`export let exit_status = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L360)
  The status given to `quit`, or 0.
- **Desktop.Host** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L365)
- **Desktop** · [`export let Window = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L370)
  A window of this application, identified by the host's handle. Usable from any task. Tagged `#Final`, `#Synchronized`.
- **Desktop.Window** · [`export last handle: uint32 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L372)
  The host's handle; never reused while the application runs.
- **Desktop.Window** · [`export let init = func(window_handle: uint32, through: Host)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L378)
  The window with `window_handle`. Use `Context.open_window` or `Host.window`.
- **Desktop.Window** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L384)
  Closes the window. Its renderer and state end; a call in flight from it is cancelled.
- **Desktop.Window** · [`export let focus = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L387)
  Gives the window the input focus.
- **Desktop.Window** · [`export let set_title = func(title: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L390)
  Replaces the window's title.
- **Desktop.Window** · [`export let host = func -> Host`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L394)
  The services this window uses.
- **Desktop.Window** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L396)
- **Desktop.Window** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L398)
- **Desktop.Window** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L405)
- **Desktop** · [`export let Event = enum ( Closed(Window), Focused(Window), Blurred(Window), Resized(Window, int, int), MenuActivated(string, Window) )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L409)
  A window event in the order the host observed it.
- **Desktop** · [`export let NotifyOutcome = enum (Shown, Denied, Unsupported)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L418)
  What became of a notification.
- **Desktop** · [`export let Call = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L423)
  One bridge call from a window. It carries nothing of the page: no URL, no script, no path. A call is handed to the task that runs its operation. Tagged `#Final`, `#Synchronized`.
- **Desktop.Call** · [`export last id: uint32 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L425)
  The host's request identifier.
- **Desktop.Call** · [`export last operation: string { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L428)
  The registry operation identifier.
- **Desktop.Call** · [`export last deadline_nanos: int64 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L431)
  Nanoseconds the caller still waited when the host forwarded the call (at most D-4).
- **Desktop.Call** · [`export let init = func(call_id: uint32, from: Window, operation_id: string, body: uint8[list], nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L441)
  A call of `operation_id` from `from` carrying `body`. Built by `Context.run` from a Call frame.
- **Desktop.Call** · [`export let request = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L451)
  The request: the JSON bytes of the contract operation.
- **Desktop.Call** · [`export let text = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L454)
  The request as text, when it is UTF-8.
- **Desktop.Call** · [`export let window = func -> Window`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L462)
  The window whose main frame made the call.
- **Desktop.Call** · [`export let host = func -> Host`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L466)
  The window host's services for this call. Requests through it answer Cancelled once the call is cancelled.
- **Desktop.Call** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L468)
- **Desktop** · [`export let Dispatcher = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L493)
  Maps each operation of a registry to its implementation. An operation without one answers the caller with error 13 (Unsupported). Each implementation runs as its own task: it must be task-safe and capture only transferable values (docs/lang/Concurrency.md section 5). Tagged `#Final`.
- **Desktop.Dispatcher** · [`export last registry: Registry`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L495)
  The operations this dispatcher serves.
- **Desktop.Dispatcher** · [`export let init = func(served: Registry)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L502)
  A dispatcher for `served`, with no implementation yet.
- **Desktop.Dispatcher** · [`export let add = func(operation: string, handler: func(Call) -> Result<uint8[list]>) -> Dispatcher`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L508)
  Adds the implementation of `operation`, which must be in the registry and not yet added. Traps with "invalid argument: dispatcher operation" otherwise.
- **Desktop.Dispatcher** · [`export let on_event = func(handler: func(Event) -> void) -> Dispatcher`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L519)
  Has `Context.run` call `handler` with every window event, in the order the host observed them, after its own handling (a Closed window is no longer open). The handler runs on the task that called `run`, between calls, so it may use the context. Replaces an earlier handler.
- **Desktop.Dispatcher** · [`export let dispatch = func(call: Call) -> [uint32, uint8[list]]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L542)
  Runs `call` on the calling task: the status and reply bytes, 0 and the reply, or an error code and the UTF-8 message. `Context.run` runs each call as a task instead.
- **Desktop.Dispatcher** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L560)
- **Desktop** · [`export let Context = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L583)
  The connection of the native child to its window host, used by the task that runs `start`. Tagged `#Final`, `#NotThreadSafe`.
- **Desktop.Context** · [`export last registry: Registry`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L585)
  The registry sent in Hello.
- **Desktop.Context** · [`export let init = func(served: Registry)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L612)
  Use `run_application`.
- **Desktop.Context** · [`export let host = func -> Host`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L628)
  The services of the window host for this task. Pass `context.host().fork()` to another task.
- **Desktop.Context** · [`export let window = func(handle: uint32) -> Window`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L631)
  The window with `handle`, open or not.
- **Desktop.Context** · [`export let open_window = func(title: string, width: int, height: int) -> Result<Window>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L635)
  Opens a window of `width` by `height` pixels that shows the bundle's start route. Busy when D-1 windows are open.
- **Desktop.Context** · [`export let open_window_at = func(title: string, width: int, height: int, min_width: int, min_height: int, route: string) -> Result<Window>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L640)
  Opens a window with a minimum size and the bundle route it starts on. Sizes are 0 to 65,535; a route starts with `/`. Busy when D-1 windows are open.
- **Desktop.Context** · [`export let open_windows = func -> Window[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L656)
  The windows this context opened that it has not seen close, in the order they opened. Windows a bridge operation opened through its own `Host` are not listed.
- **Desktop.Context** · [`export let data_directory = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L659)
  The application's own data directory (`$XDG_DATA_HOME/<id>` on Linux).
- **Desktop.Context** · [`export let notify = func(title: string, body: string) -> Result<NotifyOutcome>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L663)
  Shows a system notification: Shown, Denied (notifications turned off), or Unsupported (no notification service).
- **Desktop.Context** · [`export let clipboard_write = func(text: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L667)
  Replaces the clipboard's text. CapabilityDenied unless the package grants `desktop.clipboard_write`.
- **Desktop.Context** · [`export let clipboard_read = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L670)
  The clipboard's text. CapabilityDenied unless the package grants `desktop.clipboard_read`.
- **Desktop.Context** · [`export let next_event = func -> Result<Event>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L674)
  The next window event, waiting for one. Bridge calls that arrive first wait for `run`. UnexpectedEnd once the host has closed the channel.
- **Desktop.Context** · [`export let run = func(dispatcher: Dispatcher) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L697)
  Answers bridge calls with `dispatcher` and consumes events until the host ends the channel: after the last window closes, after `quit`, or when the host itself ends. Each call runs as its own task. A Closed event forgets its window; then every event goes to the dispatcher's `on_event` handler on this task. When the channel ends, calls still running are cancelled; if any is still running then, the process ends at once with the application's status. A dispatcher for another registry is InvalidInput.
- **Desktop.Context** · [`export let quit = func(status: int) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L759)
  Ends the application with `status` (0 to 255). The host closes every window and the channel; `quit` returns when the channel has ended, `run` then returns, and the process ends with `status`. Calls and events still in flight are dropped.
- **Desktop.Context** · [`export let exit_status = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L762)
  The process status the application ends with: the status given to `quit`, or 0.
- **Desktop.Context** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L876)
- **Desktop** · [`export let run_application = func(registry: Registry, start: func(Context) -> Result<Unit>) -> program.ExitCode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/desktop.nra#L883)
  Connects to the window host, runs `start`, and ends the process with the application's status: the status given to `quit`, 0 when `start` returns Ok, and 1 when it returns an error or the handshake fails. The error goes to standard error. It returns only if ending the process fails.
<!-- END SOURCE API -->

## Detailed contracts

The following section is the module's part of the standard-library design. The source and current [language status](Status) identify shipped behavior.

### 11.5 `Std.Desktop`

*Since 0.3.0; Linux x86-64 qualified under Xvfb.* The native child of a desktop application ([Desktop design](https://github.com/Kayyo321/neura/blob/main/docs/design/Desktop030.md)). The child speaks to its window host over standard input and output with the framed protocol of the design, so a desktop child writes its log to standard error and never to standard output. It needs no capability group: the window host performs windows, notifications and the clipboard, and `neura build --native --deny-all --max-steps 18446744073709551615` is the intended build of a child. The child lives as long as the application, so its policy must not keep the default step limit of `neura run` (100,000,000 steps over the whole run), which a long-lived application reaches.

- **`Registry(operations: string[list])`** lists the bridge operations, each 1 to 128 bytes of `a`–`z`, `0`–`9`, `.`, `_` and `-`, distinct and in ascending byte order (otherwise it traps with `invalid argument: registry operation ...`). `json()` is the canonical text of `registry.json`, `{"operations":[...],"schema":1}`, and `digest()` its SHA-256. The child sends this digest in its Hello; a host whose `registry.json` hashes differently ends the application before any operation runs.
- **`Dispatcher(registry)`** with `add(operation, handler: func(Call) -> Result<uint8[list]>)` maps operations to implementations; adding an operation outside the registry, or twice, traps. `on_event(handler: func(Event) -> void)` has `run` pass it every window event, in order, on the task that called `run`. `dispatch(call)` runs one call on the calling task, for tests.
- **Calls run as tasks.** `run` spawns each call as its own task ([Concurrency](Concurrency)), so a slow operation holds up no other and replies come back in the order operations finish. An implementation is therefore a spawned call: task-safe, with only transferable captures. It reaches the window host through `call.host()` and `call.window()`; capturing the `Context` is `E0602` at the program's call of `run`. A call whose caller stopped waiting (past D-4, its window closed, or its page ended) is cancelled: a parked timer, channel or host request answers `Cancelled`, and a computing operation sees `Std.Concurrent.Cancellation.requested()`. Nothing interrupts an operation that never parks and never asks. A desktop child cannot use `Std.DynamicForeign` or `import:c(...)`, which tasks exclude.
- **`Call`** (a monitor): `id`, `operation`, `deadline_nanos`, `request()` (the JSON bytes of the contract operation), `text()` (`Some` when they are UTF-8), `window()` and `host()`, and nothing of the page.
- **`Host`** (a monitor, usable from any task): `open_window(title, width, height)` and `open_window_at(title, width, height, min_width, min_height, route)` answer `Result<Window>` (Busy past the 8 windows of D-1; the child keeps the default D-1 and D-3, and the window host refuses a package that raises them); `window(handle)`; `quit(status)`; `data_directory()`; `notify(title, body) -> Result<NotifyOutcome>` (`Shown`, `Denied`, `Unsupported`); `clipboard_write(text)` and `clipboard_read()`, which answer `CapabilityDenied` unless the package grants `desktop.clipboard_write` or `desktop.clipboard_read`; `fork()` gives another task its own `Host`. A request whose channel has ended answers `UnexpectedEnd`.
- **`run_application(registry, start: func(Context) -> Result<Unit>) -> ExitCode`** connects, calls `start`, and ends the process with 0, the status given to `quit`, or 1 when the handshake or `start` fails.
- **`Context`** (the task that runs `start`): the `Host` services above, delegated to its own `host()`; `open_windows()` (the windows it opened and has not seen close); `next_event() -> Result<Event>` with `Event` = `Closed(Window)`, `Focused(Window)`, `Blurred(Window)`, `Resized(Window, int, int)`, `MenuActivated(string, Window)`; `run(dispatcher)` serves calls and passes events to the dispatcher's `on_event` handler until the host ends the channel, then cancels the calls still running and, if one still runs, ends the process at once.
- **`Window`** (a monitor, usable from any task): `handle`, `close()`, `focus()`, `set_title(title)`, `host()`; windows are equal by handle.
- A handler's `Err` reaches the page as status 100 with the error's message; an operation without an implementation as status 13; a reply larger than D-3 as status 16 with no body. A trap in the child ends the application.

The design's named-argument form `open_window(title:, width:, ...)` waits for named arguments; the entry form `start: func(desktop.Context)` called by a desktop target waits for project targets ([Projects](https://github.com/Kayyo321/neura/blob/main/docs/design/Projects030.md)). Until then a program's `main` returns `run_application(registry, start)`.
