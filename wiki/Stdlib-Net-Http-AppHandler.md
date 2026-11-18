# `Std.Net.Http.AppHandler`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.AppHandler`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/app-handler.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.AppHandler`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AppHandler** · [`export let RouterHandler = class implements app.Handler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/app-handler.nra#L31)
  A router answering the requests `Std.App.Context.serve` receives. Tagged `#Final`, `#NotThreadSafe`.
- **AppHandler.RouterHandler** · [`export let init = func(routes: http.Router)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/app-handler.nra#L36)
  Wraps `routes`.
- **AppHandler.RouterHandler** · [`export let handle = func(request: app.Request) -> Result<app.Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/app-handler.nra#L41)
  Converts, dispatches and converts back.
- **AppHandler.RouterHandler** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/app-handler.nra#L56)
  The text "RouterHandler(Router(N routes))".
- **AppHandler** · [`export let handler = func(router: http.Router) -> app.Handler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/app-handler.nra#L61)
  `router` as a handler. Tagged `#NoDiscard`.
- **AppHandler** · [`export let to_http_request = func(request: app.Request) -> http.Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/app-handler.nra#L65)
  The `http.Request` for a host request. Tagged `#NoDiscard`.
- **AppHandler** · [`export let to_app_response = func(response: http.Response) -> Result<app.Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/app-handler.nra#L79)
  The `app.Response` for a router response: status, fields without the host-owned ones, and the body (a stream collected in memory). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
