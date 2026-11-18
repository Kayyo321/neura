# `Std.Ui.ModelHost`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Ui.ModelHost`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Ui.ModelHost`](#exported-declarations)
- [State and components](https://github.com/Kayyo321/neura/blob/main/docs/apps/Components.md)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ModelHost** · [`export let Host = class implements ui.Host, ui.TaskHost, ui.HistoryHost, ui.FocusHost`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L48)
  A host whose document is memory and whose events are scripted. Tagged `#Final`, `#NotThreadSafe`.
- **ModelHost.Host** · [`export let focused: string = ""`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L64)
  The `id` of the element last focused through `focus`, or empty.
- **ModelHost.Host** · [`export last log: string[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L66)
  One line per host request, in order.
- **ModelHost.Host** · [`export let refused: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L68)
  How many events `post` refused.
- **ModelHost.Host** · [`export let reconcile_commits = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L71)
  Reconciles every later commit as the browser host does and logs its operations, one line each.
- **ModelHost.Host** · [`export let set_root_limit = func(limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L76)
  Sets the number of live roots (U-9, default 16).
- **ModelHost.Host** · [`export let click = func(listener: uint64, target: ui.Component, handler: func(ui.ClickEvent) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L81)
  Adds a click on `target`'s listener `listener` to the batch being scripted.
- **ModelHost.Host** · [`export let pointer_move = func(listener: uint64, target: ui.Component, handler: func(ui.PointerEvent) -> void, x: float64, y: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L89)
  Adds a pointer move, which a root coalesces per listener, to the batch being scripted.
- **ModelHost.Host** · [`export let input = func(listener: uint64, target: ui.Component, handler: func(ui.InputEvent) -> void, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L98)
  Adds an input event with `value` to the batch being scripted.
- **ModelHost.Host** · [`export let change = func(listener: uint64, target: ui.Component, handler: func(ui.ChangeEvent) -> void, value: string, checked: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L107)
  Adds a change event with `value` and `checked` to the batch being scripted.
- **ModelHost.Host** · [`export let submit = func(listener: uint64, target: ui.Component, handler: func(ui.SubmitEvent) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L116)
  Adds a submit event to the batch being scripted.
- **ModelHost.Host** · [`export let blur = func(listener: uint64, target: ui.Component, handler: func(ui.FocusEvent) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L124)
  Adds a blur event to the batch being scripted.
- **ModelHost.Host** · [`export let input_on_resynchronize = func(listener: uint64, target: ui.Component, handler: func(ui.InputEvent) -> void, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L132)
  Adds an input event that the host posts when a root resynchronizes after an overflow.
- **ModelHost.Host** · [`export let end_batch = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L141)
  Ends the batch being scripted; the next `wait` of a root posts it.
- **ModelHost.Host** · [`export let await_completion = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L148)
  Ends the batch being scripted, if it has steps, and adds a barrier: a root running commands waits there until one completes, before it takes the next batch.
- **ModelHost.Host** · [`export let start_at = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L158)
  Starts the history at `path`, as a page loaded at that address (a deep link).
- **ModelHost.Host** · [`export let use_history_routing = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L167)
  Makes links use history routing (`/parts/7`) instead of hash routing (`#/parts/7`).
- **ModelHost.Host** · [`export let back = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L172)
  Adds a press of the browser's back button to the batch being scripted.
- **ModelHost.Host** · [`export let forward = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L177)
  Adds a press of the browser's forward button to the batch being scripted.
- **ModelHost.Host** · [`export let navigate_externally = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L182)
  Adds a navigation the program did not make, such as an edited address, to the batch being scripted.
- **ModelHost.Host** · [`export let history_text = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L187)
  The history entries, oldest first, with the current one in brackets.
- **ModelHost.Host** · [`export let navigate_now = func(root: ui.Root, direction: int, path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L214)
- **ModelHost.Host** · [`export let location = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L245)
- **ModelHost.Host** · [`export let push_location = func(path: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L250)
- **ModelHost.Host** · [`export let replace_location = func(path: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L257)
- **ModelHost.Host** · [`export let link = func(path: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L264)
- **ModelHost.Host** · [`export let focus = func(root: uint64, element_id: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L271)
- **ModelHost.Host** · [`export let wait_with = func(root: ui.Root, completions: channels.Receiver<uint64>) -> ui.Wake`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L277)
- **ModelHost.Host** · [`export let clear_log = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L292)
  Forgets the recorded lines.
- **ModelHost.Host** · [`export let fail_next_commit = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L299)
  Makes the next commit fail as a host failure while applying.
- **ModelHost.Host** · [`export let refuse_next_commit = func(kind: ui.DiagnosticKind)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L304)
  Makes the next commit be refused with a diagnostic of `kind`.
- **ModelHost.Host** · [`export let open = func(target: string) -> result.Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L309)
- **ModelHost.Host** · [`export let commit = func(root: uint64, batch: ui.Commit) -> result.Result<ui.RenderOutcome>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L345)
- **ModelHost.Host** · [`export let dispatch_click = func(root: ui.Root, handler: uint64, event: ui.ClickEvent) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L382)
  Delivers `event` to the listener `handler` of the reconciled tree of `root`, through the root's queue, as the browser host does. False when no mounted listener has that handler.
- **ModelHost.Host** · [`export let stats = func(root: uint64) -> ui.HostStats`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L393)
- **ModelHost.Host** · [`export let wait = func(root: ui.Root) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L403)
- **ModelHost.Host** · [`export let resynchronize = func(root: ui.Root)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L413)
- **ModelHost.Host** · [`export let reset = func(root: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L420)
- **ModelHost.Host** · [`export let close = func(root: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L425)
- **ModelHost.Host** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L444)
  `ModelHost(` the number of roots opened `)`.
<!-- END SOURCE API -->

## Detailed contracts

- An in-memory `ui.Host` for tests. Each `wait` posts the next scripted batch of events; a root's `run` returns when the script is exhausted.
- Every host request is a line of `log`. `fail_next_commit` and `refuse_next_commit` make the next commit a host failure or a refusal.
- It does not interpret views or reconcile them against a document.
