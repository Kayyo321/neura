# `Std.Events.Event`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Events.Event`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Events.Event`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Event** · [`export let Subscription = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L10)
  The token of one registration. Unsubscribing removes that registration; doing it again does nothing. Two subscriptions are equal only when they are the same object. Tagged `#Final`, `#NotThreadSafe`.
- **Event.Subscription** · [`export let init = func(remove: func(Subscription) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L15)
  Use Event.subscribe; the event makes its tokens.
- **Event.Subscription** · [`export let unsubscribe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L21)
  Removes the registration. It takes effect from the next emission: a handler removed during an emission is still called by it. A second call does nothing.
- **Event.Subscription** · [`export let active = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L30)
  Whether the registration is still in place.
- **Event** · [`export let Event = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L53)
  The owner's side of an event with payloads of type T. The registrations are an immutable list that subscribe and unsubscribe replace, so an emission delivers to the registrations that existed when it started and copies nothing. Tagged `#Final`, `#NotThreadSafe`.
- **Event.Event** · [`export let subscribe = func(handler: func(T) -> void) -> Subscription`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L59)
  Registers `handler` until its subscription is removed, and returns the subscription. The event keeps the handler alive; collection is not unsubscription. The same handler subscribed twice is two registrations.
- **Event.Event** · [`export let emit = func(payload: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L75)
  Delivers `payload` to every registration that exists when the emission starts, in registration order. A registration added or removed by a handler takes effect from the next emission; a nested emit uses the registrations at its own start. A trap in a handler ends the run.
- **Event.Event** · [`export let view = func -> EventView<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L83)
  A view that can subscribe and nothing else, for the owner to export.
- **Event.Event** · [`export let subscriber_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L86)
  The number of registrations in place.
- **Event** · [`export let EventView = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L101)
  What an owner exports: subscribe only. It cannot emit and gives no access to the Event. Tagged `#Final`, `#NotThreadSafe`.
- **Event.EventView** · [`export let init = func(event: Event<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L105)
  Use Event.view.
- **Event.EventView** · [`export let subscribe = func(handler: func(T) -> void) -> Subscription`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/events/event.nra#L110)
  Registers `handler` on the event; see Event.subscribe.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 20. `Std.Events`

`Std.Events.Event` is a library module in `stdlib/std/events/event.nra` ([design](https://github.com/Kayyo321/neura/blob/main/docs/design/Lifetime030.md), implemented, proposed). It needs no language feature of its own. An owner keeps an `Event<T>`, emits payloads to the handlers subscribed to it, and exports its `EventView<T>`, which can only subscribe.

| Declaration | Contract |
| --- | --- |
| `Event<T>` | The owner's side. `new Event<T>()`; `subscribe(handler: func(T) -> void) -> Subscription`; `emit(payload: T)`; `view() -> EventView<T>`; `subscriber_count() -> int`. `#Final`, `#NotThreadSafe`. |
| `EventView<T>` | What an owner exports: `subscribe` only. It cannot emit and gives no access to the `Event`. `#Final`, `#NotThreadSafe`. |
| `Subscription` | The token of one registration. `unsubscribe()` removes that registration and does nothing when called again; `active() -> bool`. Two subscriptions are equal only when they are the same object. `#Final`, `#NotThreadSafe`. |

- `subscribe` keeps its handler until `unsubscribe`; collection is not unsubscription, and an event keeps its handlers alive. The same handler subscribed twice is two registrations with two tokens.
- `emit` delivers synchronously, in registration order, to the registrations that exist when the emission starts. A registration added or removed by a handler takes effect from the next emission, so a handler removed during an emission is still called by it. A nested `emit` uses the registrations at its own start.
- Handlers return nothing. A trap in a handler ends the run.
- `emit` compares nothing and suppresses nothing. An owner commits its state first and then emits the old and new values it accepted, as a tuple payload; it does not read them again through a getter.
- The registrations are an immutable list that `subscribe` and `unsubscribe` replace, so `emit` copies nothing. `subscribe` and `unsubscribe` allocate; `emit` does not, beyond what handlers do.
- The three classes are not transferable, so giving one to a task is [E0602](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0602.md).
- `emit` is not called under a monitor guard ([E0550](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0550.md)): in a `#Synchronized` method, in a lambda passed directly to `Guarded.with`, or in a function either of them reaches through calls the compiler resolves. A call through a function value is not followed, so the check is a check of the known call graph and not a proof.
- Tests: `stdlib-events`, and `lifetime-programs` for delivery in every execution mode.

