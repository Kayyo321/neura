# `Std.Ui.Commands`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Ui.Commands`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Ui.Commands`](#exported-declarations)
- [State and components](https://github.com/Kayyo321/neura/blob/main/docs/apps/Components.md#commands)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Commands** · [`export let RunOutcome = enum (Started, Rejected)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L15)
  What `Command.run` did: started the work, or started nothing.
- **Commands** · [`export let CommandState = enum<O> (Idle, Running, Succeeded(O), Failed(error.PortableError), Cancelled)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L18)
  The state of a command: never run, running, or how its latest run ended.
- **Commands** · [`export let Command = class<I, O>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L69)
  Work that waits. `work` runs as a task in the scope of `Commands.run`, so it must be task-safe and its captures, `I` and `O` transferable (E0601, E0602); its failure reaches the component as a `PortableError`. `state` is a `ui.State` that components render. Each `run`, each `cancel` of a running command and each unmount of the component the command is bound to raise its generation, and a completion of an older generation changes nothing. `work` learns of a cancellation through `Std.Concurrent.Cancellation.requested()` or a parked operation that returns `Cancelled`. Tagged `#Final`, `#NotThreadSafe`.
- **Commands.Command** · [`export last state: ui.State<CommandState<O>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L72)
  `Idle` until the first run, `Running` while the latest run works, then how it ended.
- **Commands.Command** · [`export let init = func(work: func(I) -> result.Result<O>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L79)
  A command that runs `work`; `work` is retained. Bind it to a component before running it.
- **Commands.Command** · [`export let bind = func(component: ui.Component)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L86)
  Makes `component` the command's owner: the command runs on the owner's root, and unmounting the owner cancels the command and raises its generation. A command is bound once.
- **Commands.Command** · [`export let run = func(input: I) -> RunOutcome`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L101)
  Starts `work(input)` on the owner's root and sets the state to `Running`. `Rejected`, with nothing started, when the command is not bound to a mounted component or the root already has its limit of commands in flight (U-10, 32 by default). The task starts when the root's `Commands.run` next takes a turn.
- **Commands.Command** · [`export let cancel = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L132)
  Cancels the runs in flight, raises the generation and sets the state to `Cancelled`. A command with no run in flight is left as it is.
- **Commands.Command** · [`export let generation = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L142)
  Raised by each run, each cancel of a running command and each unmount of the owner.
- **Commands.Command** · [`export let runs_in_flight = func -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L145)
  The number of this command's runs that have not finished.
- **Commands.Command** · [`export let abandon = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L149)
  Unmounting the owner: cancel the runs in flight and raise the generation.
- **Commands.Command** · [`export let finish = func(ticket: uint64, outcomes: channels.Receiver<Finished<O>>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L160)
  Applies a finished run in the root's turn; false when its generation is old.
- **Commands.Command** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L197)
  `Command(generation N, M in flight)`.
- **Commands** · [`export let run = func(root: ui.Root)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/commands.nra#L207)
  Runs `root`'s turns, as `root.run()` does, inside a scope that owns every command task the root starts. Each turn is either the next event or the completion of a command. With commands in flight and no event queued, the root waits for both together when its host is a `ui.TaskHost`, and otherwise for the next completion first. It returns when the root is closed or faulted, or when the host has no more events and no command is in flight; it cancels the commands still running and returns after their tasks have finished.
<!-- END SOURCE API -->

## Detailed contracts

- `Command<I, O>` runs `work: func(I) -> Result<O>` as a task when `run(input)` is called on a command bound to a mounted component; `state` is a `ui.State<CommandState<O>>`. Over `U-10` runs in flight, or without a mounted owner, `run` returns `Rejected`.
- Each `run`, each `cancel` of a running command and each unmount of the owner raise the generation; a completion of an older generation is discarded and counted by `root.stale_completions()`.
- `run(root)` is the root's event loop with the scope that owns the command tasks; it cancels and joins the runs in flight when it returns. `root.run()` traps when a command was started.
- Task safety: the work runs in another task, so it must be task-safe and its captures transferable. A program that uses this module cannot also use `Std.DynamicForeign` or a C import.
