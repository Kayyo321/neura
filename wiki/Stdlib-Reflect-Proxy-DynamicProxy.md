# `Std.Reflect.Proxy.DynamicProxy`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Reflect.Proxy.DynamicProxy`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/proxy/dynamic-proxy.nra) defines the shipped declarations: interface proxies. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source. [Section 20 of the standard library specification](Stdlib-Design#22-stdreflect) describes `Std.Reflect` as a whole.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DynamicProxy** · [`export let InvocationHandler = model.InvocationHandler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/proxy/dynamic-proxy.nra#L14)
  The handler a proxy forwards to. Since 0.3.0.
- **DynamicProxy** · [`export let MethodCallContext = model.MethodCallContext`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/proxy/dynamic-proxy.nra#L18)
  One method call through a proxy. Since 0.3.0.
- **DynamicProxy** · [`export let create = func<I>(handler: InvocationHandler) -> I`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/proxy/dynamic-proxy.nra#L22)
  A new `I` whose methods call `handler`. Since 0.3.0.
- **DynamicProxy** · [`export let forward = func(target: model.Variant, context: MethodCallContext) -> model.Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/reflect/proxy/dynamic-proxy.nra#L27)
  Forwards a call to `target` through its descriptor: the usual body of a handler that wraps an object, for logging or mocking around the real implementation. Since 0.3.0.
<!-- END SOURCE API -->
