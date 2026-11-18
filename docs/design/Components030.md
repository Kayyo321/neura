# Neura 0.3.0: state, components and contracts

This note fixes the library and generation contracts that sit on [HTML factories](Html030.md): `Std.Ui` roots, state, components, events, commands and routing; `Std.Collections.Frozen.FrozenList<T>`; and typed contracts with their generated HTTP clients, server adapters and desktop bridges. It is one of the notes of [the application platform](Apps030.md), which owns the limits and code ranges referred to here. Rows move to `docs/apps/Components.md` and `docs/apps/Contracts.md` in the change that implements them.

The owner approved the A0 design decisions on 2026-11-17. Rows marked *proposed* await implementation and qualification in A3. Later changes to a row are made here, with the reason.

This note adds no syntax. It depends, by name, on records and `new with`, derived JSON codecs, named arguments, `using` and `?` on a `Result`, each fixed by the note of its own milestone.

<!-- example: fragment: Std.Ui and records are not implemented until A3 -->
```neura
let ui = import:Std.Ui;

let Stock = record(name: string, count: int32);

let make_stock = func(name: string) -> ui.Component {
    last state = new ui.State<Stock>(new Stock(name, 0));
    last restock = new func(event: ui.ClickEvent) {
        state.update(func(stock) => new with(stock) { count: stock.count + 12 });
    };
    return ui.component(
        state: state,
        styles: PartStyles,
        render: new func(snapshot) => part_row(
            name: snapshot.name,
            count: snapshot.count,
            on_restock: restock,
        ),
    );
};
```

`update` calls its argument at once and does not keep it, so that function needs no `new`. The component keeps `render` and the view keeps `restock`, so both are `new func`.

## State and components

| Rule | Status |
| --- | --- |
| `ui.State<T>` holds one value of a deeply immutable `T`: `bool`, a number, `string`, a tuple or enum of such parts, an `#Immutable` class (a record of immutable fields is one), or a `FrozenList` of them. Any other `T` is `E0580` at the type argument. | implemented (docs/apps/Components.md); a record is not, records are not on this branch |
| `get()` returns the current value. `set(value)` and `update(change: func(T) -> T)` replace it; `update` calls `change` once, synchronously, and does not retain it. | implemented |
| Each accepted `set` or `update` notifies every subscriber once, in subscription order, with the new value, even when the new value is the old one. A state built with `equal: func(T, T) -> bool` skips the notification when `equal(old, new)` is true; there is no default equality. | implemented; the `equal:` form is `ui.with_equality<T>(initial, same)`, since named arguments are not on this branch |
| An update made by a subscriber during a notification is queued and applied when the round ends. More than 100 rounds in one turn is the trap `state update loop`. | implemented: the value changes at once and the notification is queued; rounds are counted per outermost notification |
| `subscribe(new func(T))` returns a `ui.Subscription` whose `close()` is idempotent. A class's `get` and `set` clauses and hooks are never observed: only `ui.State` is. | implemented |
| A state, a subscription, a component, a root and a view belong to one task (`E0602` when given to another). | implemented through `#NotThreadSafe` |
| `ui.component(state:, styles:, render:)` returns a `ui.Component`. `render` is retained and render-compatible (`E1010`): it receives a snapshot and returns a view. `styles` keeps the stylesheet reachable; it registers nothing at run time. | partly implemented: `ui.component<T>(state, render)`; no `styles` (no `ui.Stylesheet` yet) and no `E1010` (Html030 step 6) |
| A component is embedded in another's view with `{ ui.slot(child) }`. The child renders by itself when its state changes; the parent's render is not run again for it. Children and named slots are ordinary `ui.View` parameters of a factory. | implemented |
| `web.mount(selector, component, tasks)` and `desktop.mount(window, component, tasks)` return `Result<ui.Root>`. `tasks` is the handle of the enclosing `scope`, so every task the root starts is lexically inside it. A second root on one element, or more than `U-9` roots, is `Err`. | partly implemented: `ui.mount(host, target, component)`; `Std.Web.mount` is a stub (no `neura.ui.*` services); `desktop.mount` is not declared (no window type). A `Scope` cannot own tasks a library starts (a `spawn` is lexical and a `Scope` cannot be stored), so the root's tasks are owned by the scope of `Std.Ui.Commands.run(root)` instead; `tasks` gives nothing a library can use (A3 round 2, decision 13) |
| `root.run()` is the event loop. One *turn* is: take the next event; call its handler; apply queued updates and notify; render each component whose state changed, once, parents before children; commit. It returns when the root is closed or its scope is cancelled. | implemented; `run` also returns when the host has no more events |
| An `Invalid` or `Err` commit is delivered to the root's `on_render_failure` callback (default: log it and keep the previous view) and the loop continues, except that a faulted root stops taking events until `reset`. | implemented; a faulted root stops its loop, and `reset` restarts it |
| Unmounting a component removes its listeners, closes its subscriptions, cancels its commands and raises its generation. | implemented |

## Events

The adapter copies the fields below when the event arrives and queues an immutable record to the root's task. No browser object reaches Neura.

| Event | Record | Fields | `prevent_default` | Coalesced |
| --- | --- | --- | --- | --- |
| `click`, `dblclick` | `ClickEvent` | `button: uint8`, `x`, `y: float64` (relative to the element), `modifiers: ui.Modifiers`, `detail: int32` | yes | no |
| `input` | `InputEvent` | `value: string`, `checked: bool`, `composing: bool` | no | no |
| `change` | `ChangeEvent` | `value: string`, `checked: bool` | no | no |
| `submit` | `SubmitEvent` | `submitter: string` | yes | no |
| `keydown`, `keyup` | `KeyEvent` | `key`, `code: string`, `modifiers`, `repeat: bool` | yes | no |
| `focus`, `blur` | `FocusEvent` | none | no | no |
| `pointerdown`, `pointerup` | `PointerEvent` | `x`, `y`, `button`, `kind: ui.PointerKind`, `modifiers` | `pointerdown` only | no |
| `pointermove` | `PointerEvent` | as above | no | the latest per listener |
| `scroll` | `ScrollEvent` | `top`, `left: float64` | no | the latest per listener |
| `toggle` | `ToggleEvent` | `open: bool` | no | no |
| `close` | `CloseEvent` | `return_value: string` | no | no |

| Rule | Status |
| --- | --- |
| `stop_propagation` is valid on every event of the table except `focus`, `blur`, `scroll`, `toggle` and `close`. Both modifiers are applied by the adapter when the event arrives, before any Neura code runs. | implemented by A1 at template check; the adapter is not |
| Events are delivered in arrival order. A coalesced event replaces the queued one of the same listener and keeps its place. | implemented in `ui.Root.post` |
| When `U-7` events are queued, the root is *overloaded*: an arriving event that cannot be coalesced is not queued, its modifiers still apply, and it is counted. The adapter sets `data-neura-overloaded` on the root's element. Before the next event it accepts, it delivers `RootEvent.InputOverflow(dropped: uint32)` to `on_overflow`, followed by one `input` record for each controlled element whose value changed meanwhile, so no text edit is lost without the application being told. | implemented in the root and the model host; `data-neura-overloaded` needs the browser adapter |
| The order of events and the values of their fields are recorded in the trace, and a replay delivers them without a browser. Replay reproduces the Neura execution, not the screen. | proposed |
| An action that the browser allows only during a user gesture (a file picker, a clipboard write, a download) is a `web.GestureAction` prepared in advance (`web.copy_action(text)`, `web.download_action(name, bytes)`, `web.pick_file_action(accept)`) and bound with `on:click={ web.with_gesture(action, then) }`. The adapter performs it inside the browser event and queues a `web.GestureResult` to `then`. No asynchronous completion is promised a gesture. | proposed |

## Commands and lifetime

| Rule | Status |
| --- | --- |
| A handler should finish within a turn. Work that waits belongs to a `ui.Command<I, O>`, built from `work: func(I, cancellation.Token) -> O` with `I` and `O` transferable and immutable (`E0602`, `E0580`). | implemented as `Std.Ui.Commands.Command<I, O>` with `work: func(I) -> Result<O>` and no token (decisions 11, 12); `O` must be transferable and `ui.State` requires the state deeply immutable |
| `command.run(input)` starts `work` as a task in the root's scope, at most `U-10` at once; over the limit it returns `Rejected` and starts nothing. `command.state` is a `ui.State<ui.CommandState<O>>`: `Idle`, `Running`, `Succeeded(O)`, `Failed(error.PortableError)` or `Cancelled`. | implemented, with `commands.CommandState`; the scope is that of `commands.run(root)`, and an unbound or unmounted command is also `Rejected` |
| Each `run` and each unmount raises the command's generation. A completion that carries an older generation is discarded: it updates nothing. | implemented; a `cancel` of a running command raises it too |
| `command.cancel()` and unmount request cancellation; the task ends at its next cancellation point and is joined by the enclosing scope. Nothing is detached, and no `Scope` is stored in a field. | implemented: each run works in a scope of its own inside the task `commands.run` starts |
| A `Result<T>` is not sent between tasks, because `Error.Custom` can hold any object. A command's failure is a `PortableError`. | implemented |
| There is no `async` or `await`. A provider call parks its task, as host I/O does today ([Concurrency](../lang/Concurrency.md#104-host-io)). | implemented |

## Forms, routing and controls

| Rule | Status |
| --- | --- |
| The library ships, as components over real elements: text input, checkbox, select, button, form with validation, list, table and dialog. Each uses the platform's element, so focus, keyboard and accessibility come from the browser. | implemented in `Std.Ui.Controls` (and a text area), tested on the model host; focus, selection and composition journeys in headless browsers need the browser host |
| `ui.Field<T>` pairs a draft `string` with `parse: func(string) -> Result<T>`; `ui.Form` collects fields, reports the first invalid one, and moves focus to it on submit. Validation shared with a server is an ordinary function of a shared package; it never replaces the server's own. | implemented as `Std.Ui.Forms.Field<T>` and `Form`, with the focus moved through `ui.FocusHost` after the turn's commit |
| `ui.Router` maps path patterns (`/parts/{id}`, segments only, no wildcards) to components. `ui.route(path)` returns a `ui.Url` for `href`. A click on such a link is handled by the adapter without a page load. | implemented as `Std.Ui.Routing.Router`, with `router.link(path)` for `href` and `router.on_link(path)` for `on:click.prevent_default` (decision 15); the adapter's own interception of plain links is the browser host's |
| A web target chooses `routing = "hash"` (the default: works on any static host) or `"history"` (the host must serve `index.html` for unknown paths; the build writes a `404.html` copy and says so). | partly implemented: the router asks its `ui.HistoryHost` for the link form; the target setting and `404.html` belong to the web build |
| Loading, empty and failed states are explicit values of the application's state; the library provides no implicit suspense. | implemented: a command's state is one |

## FrozenList

`Std.Collections.Frozen.FrozenList<T>` is a list that nobody can change, with storage nobody else holds.

| Rule | Status |
| --- | --- |
| `T` is deeply immutable, as defined for `ui.State` (`E0580`). | implemented: `#Frozen` on the class; a type parameter passed on carries the requirement to the outermost written argument |
| `frozen.copy_of(source)` takes an array or a list and copies its elements into new private storage. `frozen.empty<T>()` and `frozen.of(items: ...T)` complete the constructors. Nothing freezes a list in place. | implemented, with `copy_of_list` for a list: a generic function cannot be overloaded, and an array does not convert to a list |
| `length`, `get(index)` (an index out of range traps, as for a list), `first_or_none()`, `last_or_none()`, `contains`, `index_of`, for-each, `slice`, `to_list()` (a fresh copy). | implemented |
| `appended(value)`, `replaced(index, value)`, `removed(index)` and `concat(other)` return a new list. The first implementation copies; structural sharing may replace it only with alias and accounting tests. | implemented (copying) |
| `equals` and `hash_code` compare elements by value and exist only where `T` has value equality and hashing; `==` stays identity. | implemented with `===` and the list hash for every `T`: Neura cannot declare a method only for some type arguments |
| A `FrozenList<T>` is an `#Immutable` class, so tasks may share it and it may be a message, when `T` is transferable. | not implemented: NVR 1.5 admits no list in a transferable layout (`TRNS`), so sending one is `E0602`; needs a format decision |
| The class is written in Neura. Its storage is a private `last` field of list type carrying the tag `#Frozen`, which only `Std.Collections.Frozen` may use. The compiler checks that the field is assigned once, from a list the constructor created, and that no method stores into it, passes it to a retaining parameter, or returns it (`E0581`); with that, the `#Immutable` check accepts the field. | implemented as an allow-list: after the one constructor assignment, only index reads and length; `Std.Ui` may also write the tag |
| The existing `ImmutableList`, `ImmutableSet` and `ImmutableMap` keep their meaning: read-only views. They are not deeply immutable and are not accepted where this note requires it. | implemented |
| Derived JSON codecs accept `FrozenList<T>` as an ordered array when `T` has a codec, bounded by `C-3` and `C-4`. A nested `FrozenList` is allowed within those bounds. An `Optional` element is rejected (`E0584`) until a tagged absence encoding exists. | proposed |

## Contracts

A contract is an ordinary interface whose operations cross a process boundary by generated code.

<!-- example: fragment: contracts are not implemented until A3 -->
```neura
let Restocked = record(name: string, count: int32);
let Refusal = record(code: string, message: string);
let RestockReply = enum (Accepted(Restocked), Rejected(Refusal));

export let PartsApi = interface (
    let restock = func(part_id: int64, amount: int32) -> result.Result<RestockReply>;
);
```

| Rule | Status |
| --- | --- |
| A contract interface has no type parameters, properties, static members or base interfaces, and no two operations of one name (`E0582`). | proposed |
| An operation is `func(parameters) -> Result<Reply>`. Every parameter type and `Reply` has a wire schema: `bool`, a fixed-width integer, `float64`, `string`, a record, an enum, a named `Optional` field of a record, or a `FrozenList`, acyclic and within `C-4` (`E0583` for the signature, `E0584` for a type). `Error`, `any`, a class that is not a record, a handle and a function are never on the wire. | proposed |
| The outer `Result` is local: `Err` means transport, deadline, decode or version failure. An expected refusal is a variant of `Reply`. | proposed |
| `contracts.toml` maps each operation: its id, method, path, placements and statuses come from there and from nowhere else. Field and variant wire names are those of each type's derived codec, pinned or defaulted by that codec's own rules, and body member names are parameter names; `contracts.lock` records all of them, so a rename that changes one is caught as a breaking change. | proposed |

```toml
schema = 1

[[contract]]
interface = "Stock.Contracts.PartsApi"
id = "parts"
version = 1
base_path = "/api/parts"

[[contract.operation]]
name = "restock"
id = "parts.restock"
method = "POST"
path = "/{part_id}/restock"
auth = "required"
max_request_bytes = 4096
max_response_bytes = 65536

[contract.operation.input]
part_id = "path"
amount = "body"

[contract.operation.status]
Accepted = 200
Rejected = 422
```

| Rule | Status |
| --- | --- |
| Every operation of the interface is mapped, and every mapping names an operation (`E1041`). `id` is unique in the project; `method` is `GET`, `POST`, `PUT`, `PATCH` or `DELETE`; no two operations share a method and path pattern. | proposed |
| An input is placed in `path`, `query` or `body`. A path or query input is a `bool`, an integer or a `string`. A `GET` or `DELETE` has no body input. Body inputs form one JSON object with one member per parameter, by parameter name. | proposed |
| The reply is the JSON of `Reply` by its derived codec, with the status its variant maps to; every variant has a status. A `Reply` that is not an enum has one status, under the key `ok`. | proposed |
| A failure of the framework, not the service, is `{"error":{"code":...,"message":...,"request_id":...}}` with status 400, 401, 403, 404, 409, 413, 500, 503 or 504. It carries no detail of the server. A client turns it into `Err`. | proposed |
| Integers are JSON numbers written in full. Neura code parses and writes them, in the browser too, so a 64-bit value never passes through a floating-point number. | proposed |
| A request carries `Neura-Contract: id/version`. A server that serves another version answers 409 before any service code runs. | proposed |
| `neura contracts` reads the interfaces through the compiler, runs no project code, and writes into `generated/contracts/` of the contract's package: an HTTP client class that implements the interface, a server adapter that builds routes from an implementation, a bridge client and a bridge dispatcher, a fake that records calls and returns scripted replies, `openapi.json`, `registry.json`, and `contracts.lock`. The output is a function of the sources and the manifest. | proposed |
| The generated files are committed. A build never regenerates them: it checks their header digest and reports `E1043` when they are stale. `neura contracts --check` does the same without writing. | proposed |
| `contracts.lock` pins each operation's wire schema. Adding an operation, or a named `Optional` field to a record, is compatible. Removing, renaming or retyping a field, adding a required field, adding or removing an enum variant, or changing an id, method, path, placement or status is breaking: `E1042` unless `version` is raised in the same change. | proposed |
| A generated client is constructed explicitly, with a base URL or a bridge, a deadline (default 30 s) and a credentials policy. Calling it looks like calling the interface and may fail like a network. Nothing retries. A lost reply is `Err(OutcomeUnknown)`, never a claim that nothing happened. | proposed |

### HTTP clients

`Std.Net.Http`'s client uses one host service, `neura.http.fetch` version 1: method text, URL text, headers as name and value pairs, body bytes, a deadline in nanoseconds and a credentials mode (`uint32`: omit, same origin, include); it answers status, headers and body bytes within `W-3`, or an error code. The browser adapter implements it with the browser's `fetch`, so origin and credential rules are the browser's. The server and desktop hosts implement it over their own sockets and TLS. It is overlapping and cancellable, and its exchanges are recorded for replay.

### Desktop bridges

The bridge client sends the same JSON bytes in a frame instead of an HTTP request ([Desktop](Desktop030.md#bridge)). Only operations listed in the desktop target's `bridge.operations` have a dispatcher entry; a bridge client for any other is `E1045`. There is no operation that takes a name and arguments of unknown type.

## Lowering and artifacts

Nothing here adds a NeuraIR operation, an NVR section or a tier change. `#Frozen` is one new tag and `E0580` one new check on type arguments, both in the frontend; the generated adapters are ordinary source. A `FrozenList` is a heap object holding one list; it is counted as today's objects are.

## Diagnostics

| Code | Title | Span | Fix |
| --- | --- | --- | --- |
| `E0580` | type is not deeply immutable | The type argument, with a label at the mutable field or part | Make the class `#Immutable` or a record; replace a list with a `FrozenList` |
| `E0581` | frozen storage is exposed or changed | The store, return or argument that leaks the `#Frozen` field; the tag outside its module | Copy instead |
| `E0582` | interface cannot be a contract | The offending member or clause | Remove it, or split the interface |
| `E0583` | operation cannot be a contract operation | The parameter or result type: not `Result<...>`, a variadic, a default, a function type | The shape required |
| `E0584` | type has no wire schema | The type, with the path to the offending part | A record, enum or `FrozenList` |
| `E1040` | malformed contract manifest | The key or value in `contracts.toml` | The schema's form |
| `E1041` | invalid operation mapping | The mapping: unmapped, unknown, duplicate id, conflicting route, invalid placement, over `C-1` | The message names the other mapping |
| `E1042` | contract change is not compatible | The changed member, with the pinned form | Raise `version`, or restore the member |
| `E1043` | generated contract code is out of date | The generated file's header | `neura contracts` |
| `E1045` | operation is not a bridge operation | The construction of the bridge client | Add the id to `bridge.operations`, or use the HTTP client |

`E1044` is assigned in [Backend](Backend030.md#diagnostics).

## Edge cases

Accepted:

1. `new ui.State<int32>(0)`, `new ui.State<Stock>(...)` with a record, and with a tuple of two strings.
2. `ui.State<frozen.FrozenList<Stock>>`.
3. `update` with a capturing plain `func`: not retained.
4. `set` with a value equal to the current one: one notification.
5. A state with `equal:` given: no notification when equal.
6. A subscriber that calls `set` on the same state: queued, applied after the round.
7. `close()` of a subscription twice.
8. Two components sharing one state: both render in one turn, in mount order.
9. A child embedded with `ui.slot`, updated alone: the parent's render is not called.
10. A handler that makes three updates: one render, one commit.
11. An event for a listener removed by an earlier event of the same batch: discarded.
12. `pointermove` arriving faster than turns: the handler sees the latest.
13. A command run twice quickly: the first completion is discarded.
14. A command completing after its component unmounted: discarded; after remount: still discarded.
15. `command.cancel()` when idle: nothing.
16. `frozen.copy_of` of an empty array; of a list; `frozen.of("a", "b")`.
17. A `FrozenList<FrozenList<int64>>`.
18. `appended` on a shared list: the original is unchanged.
19. A `FrozenList<Stock>` sent on a channel and returned from a command.
20. A contract operation with no parameters, and one whose `Reply` is a record rather than an enum (one status, under the key `ok`).
21. A `uint64` field of 18,446,744,073,709,551,615 round-tripped through the browser client and the bridge.
22. A record with a named `Optional<string>` field absent from the JSON: `None`.
23. A reply with an unknown extra member: ignored, by the codec's rule.
24. A compatible change (a new operation) with `contracts.lock` updated and `version` unchanged.

Rejected at compile time:

25. `ui.State<int32[list]>`, `ui.State<HashMap<string, int>>`, a class with a `let` field: `E0580`.
26. `frozen.FrozenList<StringBuilder>`: `E0580`.
27. A `render` that calls `state.set`: `E1010`.
28. `ui.component(render: func(snapshot) => ...)` with a capturing function and no `new`: the existing retention rejection.
29. A state passed to `spawn`: `E0602`.
30. A command whose `work` captures the state: `E0602` at the capture.
31. `#Frozen` in a user module; a method of `FrozenList` that returns its storage: `E0581`.
32. A contract interface with a type parameter, a property, or two `get` operations: `E0582`.
33. An operation returning `Stock` without `Result`; one taking a `func`; one with a variadic: `E0583`.
34. An operation taking an `Error`, a `HashMap`, an `any`, or a `FrozenList<Optional<int32>>`: `E0584`.
35. A recursive record reached through an `Optional` field: `E0584`.
36. `contracts.toml` with an unknown key, or `method = "TRACE"`: `E1040`.
37. Two operations with `POST /{id}/restock` and `POST /{part}/restock`: `E1041`.
38. A body input on a `GET`; a record placed in `path`: `E1041`.
39. An enum variant without a status: `E1041`.
40. A field removed from `Restocked` with `version` unchanged: `E1042`.
41. An interface edited without running `neura contracts`: `E1043`.
42. A bridge client for an operation the desktop target does not list: `E1045`.

At run time:

43. `get(length)` on a `FrozenList`: the trap of a list index.
44. 101 notification rounds in a turn: the trap `state update loop`.
45. A server that serves version 2 to a version 1 client: `Err` with the code `contract_version`, and no service call.
46. A response of `C-2` plus one byte: `Err(TooLarge)`; the body is not decoded.
47. A deadline passing during a `POST`: `Err(OutcomeUnknown)`; nothing is retried.
48. The 1,025th queued click: counted, `InputOverflow` delivered, the root keeps working.

## Order of work

Each step is one commit with its targeted tests. None changes the image of a program that does not use the feature.

| Step | Content | Tests |
| --- | --- | --- |
| 1 | This note | Document checks |
| 2 | `#Frozen`, `E0580`, `E0581`, `FrozenList` | Model-based collection tests against a list; alias tests; cases 16 to 19, 25, 26, 31, 43 |
| 3 | `ui.State`, subscriptions, `ui.component`, roots and turns on the model host | Order traces of notifications and renders; cases 1 to 11, 27 to 29, 44 |
| 4 | Event records, queueing, coalescing and overflow; gesture actions | Scripted event files; cases 12, 48; real events in headless Chromium |
| 5 | Commands and generations | Cases 13 to 15, 30; a slow command across unmount and remount |
| 6 | Controls, forms and the router | Focus, selection and composition journeys in headless browsers |
| 7 | Codec extension to `FrozenList` | Bounds, exact integers, cases 21 to 23 |
| 8 | `contracts.toml`, eligibility, `neura contracts` and its outputs | Golden generated files; cases 20, 32 to 42 |
| 9 | `neura.http.fetch` natively and the generated client against the fake | Cases 45 to 47 |

## Gate and acceptance

- No mutation of a source array or list is visible through a `FrozenList` copied from it, under a model-based test.
- Every eligibility rule has an accepted and a rejected fixture: immutability, transfer, value equality, wire schema.
- Notification, render and commit order equal the hand-derived trace for nested components.
- A stale completion changes nothing, and unmount leaves no listener, subscription or task (budget B-W9).
- Controlled inputs keep focus, selection and composition in headless Chromium and Firefox, by injected events; input methods and screen readers are qualified by a person.
- Generated clients, servers and bridges round-trip every contract fixture, including 64-bit boundary values, and reject a version mismatch before the implementation runs.
- `neura contracts` twice gives identical bytes, and runs no project code (checked with a project whose startup traps).

## Migration

A program that passed an `ImmutableList` to another task or stored one as shared state copies it with `frozen.copy_of`. A program that modelled reactive values with accessor clauses moves the value into a `ui.State`; the accessors keep their ordinary meaning. Hand-written HTTP calls to a known service become a contract and a generated client.

## Approved design decisions

1. There is no default equality for state: every accepted update notifies, and suppression is opt-in with an explicit function.
2. A render happens once per turn, after all of the turn's updates.
3. Input overflow is reported and resynchronized, not queued without bound and not silent.
4. Gesture-bound actions are prepared values, bound in the template; nothing asynchronous claims a gesture.
5. `FrozenList` is a library class checked through one internal tag, rather than a built-in type.
6. A `FrozenList` index out of range traps, as a list's does.
7. Wire identity lives only in `contracts.toml`; generated code is committed and checked, never regenerated by a build.
8. A reply's variants map to statuses; framework failures use one fixed envelope.
9. 64-bit integers are JSON numbers, with no string form.
10. One contract version is served at a time; any breaking change needs a new `version`.
11. `routing = "hash"` is the default for web targets.

## A3 implementation record

*Implemented* 2026-11-17 by autonomous agents, awaiting owner confirmation: steps 2 and 3, and the root half of step 4. Records, `new with`, named arguments, untyped lambda parameters and inference of type arguments are not on this branch, so the library is written with `#Immutable` classes, positional arguments and explicit type arguments, and the examples above stay fragments.

| Edge case | Evidence |
| --- | --- |
| 1, 2, 3, 4, 5, 6, 7 | `tests/stdlib/ui-state` (`test_state_values`, `test_notifications`, `test_equality_suppresses`, `test_update_in_subscriber_is_queued`), `tests/ui/components/accepted.nra` |
| 8, 9, 10, 11, 12, 48 | `tests/stdlib/ui-state` (`test_turn_renders_once`, `test_child_renders_alone`, `test_unmount_discards_events`, `test_coalescing`, `test_overflow`) on the model host |
| 16, 17, 18, 43 | `tests/stdlib/frozen` (with a model-based test against a list), `tests/stdlib/frozen/traps/get-out-of-range.nra` |
| 25, 26, 28, 29, 31 | `tests/ui/components`, `tests/ui/frozen`, and for 26 and 29 the `frozen-string-builder` and `spawned-ui-state` rejections of `tests/frontend.cmake`; the storage half of 31 in `tests/apps/frozen/storage.py` |
| 44 | `tests/stdlib/ui-state/traps/update-loop.nra` |
| 13, 14, 15 | `tests/stdlib/ui-commands` (`test_run_twice_discards_first`, `test_unmount_and_remount_discard`, `test_cancel_when_idle`, and the slow command across unmount and remount) |
| 30 | `command_work_captures_a_state` in `tests/concurrency/frontend.cmake`, with the label at the program's capture |
| 19 to 24, 27, 32 to 42, 45 to 47 | Not implemented: codecs, contracts, `neura.http.fetch`, `E1010`, and the transfer of a `FrozenList` |

### Decisions taken without the owner

1. `#Frozen` also marks a class: every type argument it is given must be deeply immutable. `FrozenList` and `ui.State` carry it, so `Std.Ui` may write the tag too. A type parameter given as such an argument inherits the requirement, so the check reaches the outermost written argument; requirements do not cross a prebuilt `.nir` boundary.
2. The frozen-storage check is an allow-list: after the one constructor assignment, from a fresh list or a local that was only filled, a `#Frozen` field may only be indexed for reading, measured, hashed or printed.
3. `frozen.copy_of` takes an array and `frozen.copy_of_list` a list, because a generic function cannot be overloaded. `equals`, `hash_code`, `contains` and `index_of` exist for every `T` and compare with `===`.
4. A `FrozenList` is not transferable in 0.3.0. NVR 1.5 admits no list in a transferable layout, and widening that is a format decision this lane did not take.
5. `set` during a notification replaces the value at once and queues only the notification, so `get` always returns the latest value. Rounds are counted per outermost notification, which bounds every turn. A subscriber added during a round is first notified in the next.
6. `equal:` is `ui.with_equality<T>(initial, same)`; `ui.component` takes `(state, render)` without `styles` until a stylesheet value exists.
7. Mounting is `ui.mount(host, target, component)` over the `ui.Host` interface (`open`, `commit`, `wait`, `resynchronize`, `reset`, `close`); a commit carries the turn's renders as component and view pairs. `Std.Web.mount` keeps its final signature as a stub, and `desktop.mount` waits for the desktop window type.
8. A change made between turns renders in the next turn. A faulted root ends `run`; `reset` asks the host to empty the root, renders every component again and lets `run` continue.
9. An event is stale when its component has unmounted or its generation has changed; removing one listener of a still-mounted component is the host's part. The overflow notice is queued as a marker before the next accepted event, and the host resynchronizes controlled values when the marker's turn runs; the input events it posts then are delivered before the events that arrived after the overflow.
10. *Replaced in round 2 by decision 14.* When a commit is refused (`Invalid`), components mounted or unmounted by that turn's renders stay so; only the host keeps its previous view. A component embedded by two parents traps with `a component is mounted twice`.

### Round 2

*Implemented* 2026-11-17 by autonomous agents, awaiting owner confirmation: step 5 (commands), the rollback of refused commits, and the parts of step 6 that run on the model host (controls, forms, the router and the host services they use). Steps 7 to 9 need records, derived codecs and `neura contracts`, which are not on this branch; the "Not done" list below says what each needs.

11. Commands live in `Std.Ui.Commands` (`commands.Command<I, O>`, `commands.CommandState<O>`, `commands.RunOutcome`, `commands.run(root)`), not in `Std.Ui`. The compiler applies the rule that tasks and native calls cannot share an image (Concurrency C15) to every function a program links, so a `spawn` inside `Std.Ui` would forbid `Std.Ui` to every program with a C import. A generic class cannot be re-exported under another module's name, so `ui.Command` is not an alias either.
12. `work` is `func(I) -> Result<O>`, with no `cancellation.Token`: no token type exists, and cancellation is the scope's signal, which a run's own scope gives the work through `Cancellation.requested()` and parked operations. An `Err` becomes `Failed` with `error.to_portable`, because a `Result` cannot cross a channel.
13. The root's tasks are owned by the scope of `commands.run(root)`, the root's event loop with commands. A `spawn` must be lexically inside a scope of the same function body and a `Scope` cannot be stored, so neither `command.run` nor a host can start a task in the caller's scope; `Std.Web.mount`'s `tasks` parameter cannot own the root's tasks as the row says. The loop starts the queued runs, takes events and completions as turns, and on return stops every run and leaves the scope, which joins them. A run started outside the loop is queued until it runs; `root.run()` traps when one is queued.
14. A refused (`Invalid`) or failed (`Err`) commit rolls back the turn: children a render embedded are unmounted again, children it dropped are unmounted only when the host commits and otherwise stay mounted, and every component that rendered gets back its previous view and children. The rendered components stay clean, so a component whose render the host always refuses does not block the root's other commits; it renders again when its state next notifies. A child that moves from one parent to a later-rendered parent in one turn is moved, not remounted.
15. The router is `Std.Ui.Routing.Router`. Links come from `router.link(path)`, because the routing mode belongs to the host (`ui.HistoryHost.link`), not to a free function; without a history host the hash form is used. The first match in specificity order wins (a literal segment over a parameter at the first difference), and two routes of one shape trap when added. A query is kept, a fragment is dropped, a trailing slash is ignored, and parameter values are percent-decoded.
16. History, focus and the combined wait are optional interfaces (`ui.HistoryHost`, `ui.FocusHost`, `ui.TaskHost`) next to `ui.Host`, so a host that does not provide one still compiles. A navigation the host reports is a root event: it coalesces with a queued navigation and is never dropped by `U-7`. A focus request is carried out after the turn's commit.
17. `Std.Ui.Forms` and `Std.Ui.Controls` are modules of their own, for the same reason as decision 11 for generic classes. A table row is `controls.TableRow`. Cells are keyed by column position because a template `@for` needs a key and a row's texts may repeat.
18. The E0602 label for a function value follows a captured function value to its own capture, so a library that wraps the program's closure (a command's run) reports the program's capture.

Not done in round 2, and what each part needs:

| Part | Needs |
| --- | --- |
| Step 7: derived JSON codecs for `FrozenList<T>`, bounds `C-3` and `C-4`, `E0584` for an `Optional` element, cases 21 to 23 | Records and derived codecs (the records lane): a `FrozenList` codec is an extension of the derived codec, so it can only follow it |
| Step 8: `contracts.toml`, `E0582`, `E0583`, `E0584`, `E1040` to `E1043`, `E1045`, `neura contracts` and its generated files, cases 20, 24, 32 to 42 | Records, enums with record payloads and derived codecs for the wire schema; a manifest reader and a `neura contracts` command in the driver; the desktop target's `bridge.operations` for `E1045`. Nothing marks an interface as a contract without the manifest, so the eligibility codes cannot be added on their own |
| Step 9: `neura.http.fetch` and the generated client, cases 45 to 47 | The native host service and the browser adapter's `fetch` (A2, A4), and the generated client of step 8 |
| Transfer of a `FrozenList` (case 19) | A format decision on lists in transferable layouts (NVR 1.5 `TRNS`) |
| `E1010` (case 27) | The render-effect check of [HTML factories](Html030.md) (A1) |
| Controls in a real browser: focus, selection and composition journeys in headless Chromium and Firefox | The browser host (`Std.Web.mount` over A2's `neura.ui.*` services), which is the apps integration's |
| `ui.TaskHost` and `ui.HistoryHost` for the browser | The browser host: `wait_with` can race `neura.ui.event.next` and `neura.history.next`, both cancellable, against the completions channel in a scope of its own |
| Budgets B-W5 and B-W7 | A browser run of the 1,000-row list; the model-host figures of B-W8 are not evidence for them |
| Known defect: two `#Immutable` classes of one name in different modules, each a `FrozenList` element in one program (a program's `Choice` or `TableRow` next to `Std.Ui.Controls`), fail `neura build` with an NVR loader error (`type N is not assignable to type M`) | A fix in IRtoNeuronConversion; `emit-ir` keeps the two specializations apart. Until then, rename the program's class. The repro is kept with the round-2 evidence |
