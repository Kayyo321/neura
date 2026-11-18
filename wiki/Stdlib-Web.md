# `Std.Web`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Web`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Web`](#exported-declarations)
- [State and components](https://github.com/Kayyo321/neura/blob/main/docs/apps/Components.md)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Web** · [`export let Host = class implements ui.Host, ui.HistoryHost, ui.FocusHost, ui.TaskHost`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L166)
  The `ui.Host` of a browser page. Each instance serves the roots it opened; `mount` makes one per root. It is also the page's `ui.HistoryHost` over `neura.history.*`, in the routing mode the page chose (hash routing, `#/parts/7`, or history routing, `/parts/7`), its `ui.FocusHost` over `neura.ui.focus`, and a `ui.TaskHost`: a root waits for the browser's events, the page's navigations (back, forward, an edited address) and command completions together, so neither a running command nor a pending navigation holds the others. Tagged `#Final`, `#NotThreadSafe`.
- **Web.Host** · [`export let location = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L184)
  The page's current path, or `/` when the page keeps no history.
- **Web.Host** · [`export let push_location = func(path: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L187)
  Adds a history entry for `path` without loading a page; returns the current path.
- **Web.Host** · [`export let replace_location = func(path: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L190)
  Replaces the current history entry with `path`; returns the current path.
- **Web.Host** · [`export let link = func(path: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L195)
  The `href` of a link to `path` in the page's routing mode (`neura.history.link`): `#/parts/7` with hash routing, `/parts/7` below the bundle's base with history routing; the hash form when the page keeps no history.
- **Web.Host** · [`export let focus = func(root: uint64, element_id: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L199)
  Moves the keyboard focus to the element of the root whose `id` is `element_id` (`neura.ui.focus`); false when the root is closed or holds no such element.
- **Web.Host** · [`export let open = func(target: string) -> result.Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L223)
  Opens a root on the first element `target` (a CSS selector) matches.
- **Web.Host** · [`export let commit = func(root: uint64, batch: ui.Commit) -> result.Result<ui.RenderOutcome>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L237)
  Reconciles the turn's renders with the mounted tree, registers new templates, and applies the encoded operations. A refused tree changes nothing; a browser failure faults the root.
- **Web.Host** · [`export let wait = func(root: ui.Root) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L272)
  Waits for the browser's next events, decodes them and posts each to its listener's component. A root whose router listens for navigations waits for the page's next navigation at the same time and posts it with `post_navigation`. False when the root was closed or the wait failed.
- **Web.Host** · [`export let wait_with = func(root: ui.Root, completions: channels.Receiver<uint64>) -> ui.Wake`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L307)
  Waits for the browser's events, the page's navigations (when the root listens for them) and the next completion on `completions`, whichever comes first: `Events` after posting events or a navigation, `Completed(ticket)`, or `Idle` when the event wait failed and only completions remain. Each wait is a cancellable host request in a scope of its own; the losers are cancelled, and an answer that arrived meanwhile is posted too, so no event is lost.
- **Web.Host** · [`export let resynchronize = func(root: ui.Root)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L406)
  Nothing to do: the browser queues the changed inputs after its overflow record.
- **Web.Host** · [`export let reset = func(root: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L409)
  Empties a faulted root by closing it and opening a new one on the same element.
- **Web.Host** · [`export let close = func(root: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L428)
  Closes the root: the browser empties its element and detaches its listeners.
- **Web.Host** · [`export let stats = func(root: uint64) -> ui.HostStats`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L439)
  The browser's counts for the root (`neura.ui.stats`); zeros once it is closed.
- **Web.Host** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L538)
  `WebHost(` the number of roots opened `)`.
- **Web** · [`export let mount = func(selector: string, component: ui.Component, tasks: scope.Scope) -> result.Result<ui.Root>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L547)
  Mounts `component` on the first element that `selector` matches and commits its first render. `Err` when no element matches, the element is inside or contains another root, U-9 roots are open, the component is mounted, or the page did not grant the `ui` services (a native `neura run`). Call `run` on the root for its event loop, or `Std.Ui.Commands.run(root)` when its components run commands; either returns when the root is closed. `tasks` is the enclosing scope's handle. The root starts no long-lived task in 0.3.0: each wait for events, navigations and completions runs in a scope of its own.
<!-- END SOURCE API -->

## Detailed contracts

- `mount` is declared with its final signature and traps as unsupported in 0.3.0: the browser runtime has no `neura.ui.*` host services yet. Use `ui.mount` with a `ui.Host`.
