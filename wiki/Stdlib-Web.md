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

- **Web** · [`export let mount = func(selector: string, component: ui.Component, tasks: scope.Scope) -> result.Result<ui.Root>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/web.nra#L12)
  Mounts `component` on the element that `selector` names, with its tasks in `tasks`.
<!-- END SOURCE API -->

## Detailed contracts

- `mount` is declared with its final signature and traps as unsupported in 0.3.0: the browser runtime has no `neura.ui.*` host services yet. Use `ui.mount` with a `ui.Host`.
