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

- **ModelHost** · [`export let Host = class implements ui.Host`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L31)
  A host whose document is memory and whose events are scripted. Tagged `#Final`, `#NotThreadSafe`.
- **ModelHost.Host** · [`export last log: string[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L42)
  One line per host request, in order.
- **ModelHost.Host** · [`export let refused: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L44)
  How many events `post` refused.
- **ModelHost.Host** · [`export let set_root_limit = func(limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L47)
  Sets the number of live roots (U-9, default 16).
- **ModelHost.Host** · [`export let click = func(listener: uint64, target: ui.Component, handler: func(ui.ClickEvent) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L52)
  Adds a click on `target`'s listener `listener` to the batch being scripted.
- **ModelHost.Host** · [`export let pointer_move = func(listener: uint64, target: ui.Component, handler: func(ui.PointerEvent) -> void, x: float64, y: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L60)
  Adds a pointer move, which a root coalesces per listener, to the batch being scripted.
- **ModelHost.Host** · [`export let input = func(listener: uint64, target: ui.Component, handler: func(ui.InputEvent) -> void, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L69)
  Adds an input event with `value` to the batch being scripted.
- **ModelHost.Host** · [`export let input_on_resynchronize = func(listener: uint64, target: ui.Component, handler: func(ui.InputEvent) -> void, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L78)
  Adds an input event that the host posts when a root resynchronizes after an overflow.
- **ModelHost.Host** · [`export let end_batch = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L87)
  Ends the batch being scripted; the next `wait` of a root posts it.
- **ModelHost.Host** · [`export let fail_next_commit = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L93)
  Makes the next commit fail as a host failure while applying.
- **ModelHost.Host** · [`export let refuse_next_commit = func(kind: ui.DiagnosticKind)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L98)
  Makes the next commit be refused with a diagnostic of `kind`.
- **ModelHost.Host** · [`export let open = func(target: string) -> result.Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L103)
- **ModelHost.Host** · [`export let commit = func(root: uint64, batch: ui.Commit) -> result.Result<ui.RenderOutcome>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L125)
- **ModelHost.Host** · [`export let wait = func(root: ui.Root) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L146)
- **ModelHost.Host** · [`export let resynchronize = func(root: ui.Root)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L156)
- **ModelHost.Host** · [`export let reset = func(root: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L163)
- **ModelHost.Host** · [`export let close = func(root: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L167)
- **ModelHost.Host** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/model-host.nra#L182)
  `ModelHost(` the number of roots opened `)`.
<!-- END SOURCE API -->

## Detailed contracts

- An in-memory `ui.Host` for tests. Each `wait` posts the next scripted batch of events; a root's `run` returns when the script is exhausted.
- Every host request is a line of `log`. `fail_next_commit` and `refuse_next_commit` make the next commit a host failure or a refusal.
- It does not interpret views or reconcile them against a document.
