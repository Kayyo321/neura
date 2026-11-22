# `Std.Ui.Routing`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Ui.Routing`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Ui.Routing`](#exported-declarations)
- [State and components](https://github.com/Kayyo321/neura/blob/main/docs/apps/Components.md#routing)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Routing** · [`export let Params = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L18)
  The parameter values of a matched route, by name, percent-decoded. Tagged `#Final`, `#Immutable`.
- **Routing.Params** · [`export let init = func(names: frozen.FrozenList<string>, values: frozen.FrozenList<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L23)
- **Routing.Params** · [`export let get = func(name: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L26)
  The value of the parameter `name`; traps when the route has no such parameter.
- **Routing.Params** · [`export let has = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L35)
  Reports whether the route has a parameter `name`.
- **Routing.Params** · [`export let count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L38)
  The number of parameters.
- **Routing.Params** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L41)
  `{name=value, ...}` in the pattern's order.
- **Routing.Params** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L53)
  Other parameters with equal names and values in the same order.
- **Routing.Params** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L61)
  The hash of the text.
- **Routing** · [`export let Location = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L67)
  Where the router is: the path, the query after `?`, the pattern it matched (empty when none did) and the route's parameters. Tagged `#Final`, `#Immutable`.
- **Routing.Location** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L68)
- **Routing.Location** · [`export last query: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L69)
- **Routing.Location** · [`export last route: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L70)
- **Routing.Location** · [`export last params: Params`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L71)
- **Routing.Location** · [`export let init = func(path: string, query: string, route: string, params: Params)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L74)
- **Routing.Location** · [`export let matched = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L77)
  Reports whether a route matched.
- **Routing.Location** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L80)
  The path, with `?` and the query when there is one.
- **Routing.Location** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L85)
  Another location with the same path, query and route.
- **Routing.Location** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L93)
  Combines the hashes of the path, query and route.
- **Routing** · [`export let Router = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L120)
  Paths to components, and the location that chooses one. Build it, `add` the routes, and mount `component()` (or embed it with `ui.slot`). Tagged `#Final`, `#NotThreadSafe`.
- **Routing.Router** · [`export last location: ui.State<Location>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L125)
  The current location; the router's component renders it.
- **Routing.Router** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L136)
  A router with no routes, at `/`.
- **Routing.Router** · [`export let add = func(pattern: string, make: func(Params) -> ui.Component)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L145)
  Adds a route. `pattern` is absolute, with literal segments and `{name}` parameters of distinct names (`/`, `/parts`, `/parts/{id}`). An invalid pattern, or one with the same shape as an earlier route (`/{a}` after `/{b}`), traps. `make` is retained.
- **Routing.Router** · [`export let otherwise = func(make: func(string) -> ui.Component)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L159)
  Sets the component shown when no route matches; it receives the path. The default shows an empty, hidden element. `make` is retained.
- **Routing.Router** · [`export let component = func -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L166)
  The router's component, made on the first call. It embeds the component of the current route.
- **Routing.Router** · [`export let navigate = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L189)
  Goes to `path`: adds a history entry when the router is mounted on a host that keeps history, and sets the location. `path` is absolute and may end with a query; an invalid path traps.
- **Routing.Router** · [`export let replace = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L199)
  As `navigate`, but replaces the current history entry.
- **Routing.Router** · [`export let link = func(path: string) -> ui.Url`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L210)
  The `href` of a link to `path`: the host's form when mounted on a host that keeps history (`#/parts/7` for hash routing), and the hash form otherwise.
- **Routing.Router** · [`export let href = func(path: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L213)
  The text of `link(path)`.
- **Routing.Router** · [`export let on_link = func(path: string) -> func(ui.ClickEvent) -> void`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L224)
  A click handler that navigates to `path`; bind it with `on:click.prevent_default` on the link whose `href` is `link(path)`.
- **Routing.Router** · [`export let resolve = func(path: string) -> Location`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L234)
  The location `path` resolves to with the current routes: the route that matches it, a literal segment winning over a parameter, and its parameters.
- **Routing.Router** · [`export let route_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L273)
  The number of routes.
- **Routing.Router** · [`export let current_page = func -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L276)
  The component of the current route as last rendered, or nil before the first render.
- **Routing.Router** · [`export let page_for = func(at: Location) -> ui.Component`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L279)
- **Routing.Router** · [`export let attach = func(root: ui.Root)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L311)
- **Routing.Router** · [`export let detach = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L330)
- **Routing.Router** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/ui/routing.nra#L339)
  `Router(` the number of routes `, at ` the location `)`.
<!-- END SOURCE API -->

## Detailed contracts

- `Router` maps absolute path patterns with `{name}` parameters to components; a literal segment wins over a parameter, and two routes of one shape trap when added.
- Mounted on a `ui.HistoryHost` the router starts at the host's location, pushes and replaces entries, and follows back and forward as turns; `link(path)` is in the host's routing mode.
- Task safety: a router belongs to the task that made it.
