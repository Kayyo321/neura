# State and components

`Std.Ui` keeps application state in `ui.State<T>` values, renders it with components built by `ui.component`, and runs a root's event loop over a host: a browser document, a desktop window, or the model host that tests use. A component's view comes from an [HTML factory](Html.md). This page states what Neura 0.3.0 implements; [the design note](../design/Components030.md) holds the full contract and the rows that remain open.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let model = import:Std.Ui.ModelHost;
let program = import:Std.Program;
let result = import:Std.Core.Result;
let ui = import:Std.Ui;

#Immutable
let Stock = class (
    export last name: string;
    export last count: int32;

    export let init = func(name: string, count: int32) => set;
);

let stock_view = html(name: string, count: int32, on_restock: func(ui.ClickEvent) -> void) {
    <section class="stock">
        <h1>{name}</h1>
        <output>{count}</output>
        <button type="button" on:click={on_restock}>Restock</button>
    </section>
};

let make_stock = func(name: string) -> ui.Component {
    last state = new ui.State<Stock>(new Stock(name, 0));
    last restock = new func(event: ui.ClickEvent) {
        state.update(func(stock: Stock) -> Stock => new Stock(stock.name, stock.count + 12));
    };
    return ui.component<Stock>(state, new func(snapshot: Stock) -> ui.View {
        console.print_line("render " + snapshot.name + " " + snapshot.count.to_string());
        return stock_view(snapshot.name, snapshot.count, restock);
    });
};

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let host = new model.Host();
        let stock = make_stock("bolt");
        let root = result.expect<ui.Root>(ui.mount(host, "#app", stock), "mount");
        host.click(1, stock, new func(event: ui.ClickEvent) {
            console.print_line("clicked");
        });
        host.end_batch();
        root.run();
        for (let line: string in host.log) {
            console.print_line(line);
        }
        return program.ExitCode.Ok;
    };
);
```

```text
render bolt 0
clicked
open #app: root 1
commit 1 on root 1: 1 render
```

The click in this example calls a handler that changes nothing, so the turn renders nothing and commits nothing. `update` calls its function at once and does not keep it, so that function needs no `new`; `ui.component` keeps `render` and the view keeps `restock`, so both are `new func`.

## State

| Rule | Status |
| --- | --- |
| `ui.State<T>` holds one value of a deeply immutable `T`: `bool`, a number, `string`, a tuple or enum of such parts, an `#Immutable` class whose fields are too, or a [`FrozenList`](../lang/Collections.md#frozenlist) of them. Any other `T` is `E0580` at the type argument, also when a generic function passes its type parameter on. | implemented |
| `get()` returns the current value. `set(value)` replaces it at once; `update(change)` calls `change` once with the current value, does not keep it, and sets the result. | implemented |
| Each accepted `set` or `update` notifies every open subscriber once, in subscription order, with the new value, even when it equals the old one. `ui.with_equality<T>(initial, same)` builds a state that skips the notification when `same(old, new)` is true; there is no default equality. | implemented |
| A `set` made while the state is notifying (by one of its subscribers, directly or through another state) replaces the value at once and queues the notification, which runs when the current round ends; `get` sees the new value immediately. More than 100 rounds in one notification traps with `state update loop`. | implemented |
| `subscribe(listener)` keeps `listener` (write `new func` for a capturing one) and returns a `ui.Subscription` whose `close()` may be called any number of times. A subscriber added during a round is first notified in the next one. `subscriber_count()` counts the open subscriptions. | implemented |
| A state, a subscription, a component, a root and a view stay in the task that made them: giving one to another task is `E0602`. | implemented |

## Components and roots

| Rule | Status |
| --- | --- |
| `ui.component<T>(state, render)` returns a `ui.Component`. `render` is kept, receives the state's value, and returns a view. The compiler does not yet check that a render only reads and allocates (`E1010` in [the HTML design](../design/Html030.md)). | implemented, without the render check and without `styles` |
| `ui.slot(child)` is a view that embeds another component. The child renders by itself when its own state notifies; the parent's render does not run again for it. A child is mounted when a render first embeds it and unmounted when a render no longer does. | implemented |
| `ui.mount(host, target, component)` opens a root on `target`, renders the component and its children, and commits them. It returns `Err` when the host refuses the target (a second root on one element, more than the host's root limit, 16 by default) or the component is already mounted. | implemented |
| A *turn* takes the next event, calls its handler, renders each component whose state notified since the last render, once, parents before children, and commits those renders as one host request. A turn with no render commits nothing. `root.turn()` runs one; `root.run()` runs turns until the root is closed or faulted or the host has no more events. A change made between turns renders in the next turn. | implemented |
| A commit the host refuses (`Invalid`) is given to the root's `on_render_failure` handler and the loop continues. A host failure while applying (`Err`) faults the root: the handler gets `Fault`, turns stop, and `root.reset()` empties the root through the host and renders every component again. The default handler prints the failure. | implemented |
| Unmounting a component, by a parent's render or by `root.close()`, unmounts its children, closes its subscription, and raises its `generation()`. An event queued for a component that has since unmounted is discarded and counted. | implemented; commands are not |

## Events

A host copies an event's fields into an immutable record (`ui.ClickEvent`, `ui.InputEvent`, `ui.PointerEvent` and the others in [HTML factories](Html.md)) and posts it to the root with `root.post(listener, component, coalesce, deliver)`. No browser object reaches Neura.

| Rule | Status |
| --- | --- |
| Events are delivered in arrival order, one per turn. A coalesced event (pointer moves, scrolls) replaces the queued event of the same listener and keeps its place. | implemented in the root; the browser adapter that posts them is not |
| At the queue limit (1,024 by default, `set_queue_limit` up to 65,536) the root is overloaded: an arriving event that cannot be coalesced is not queued and is counted. Before the next event the root accepts, it delivers `ui.RootEvent.InputOverflow(dropped)` to the `on_overflow` handler and asks the host to resynchronize, so that the host posts one input event for each controlled element whose value changed. | implemented in the root and the model host |

## The host interface

`ui.Host` is the interface a root uses: `open(target)`, `commit(root, batch)`, `wait(root)` (post the events that arrive; false when none will), `resynchronize(root)`, `reset(root)` and `close(root)`. A `ui.Commit` lists the `ui.Rendered` component and view pairs of one turn, parents before children.

`Std.Ui.ModelHost` implements it in memory for tests: a test scripts clicks, input and pointer moves in batches, each `wait` posts the next batch, and every request is recorded in `log`. `fail_next_commit()` and `refuse_next_commit(kind)` exercise render failures. The model host does not interpret views; reconciliation against a document belongs to the browser and desktop hosts.

`Std.Web.mount(selector, component, tasks)` is declared with its final signature and traps as unsupported: the browser runtime has no `neura.ui.*` host services yet. Desktop mounting, commands, gesture actions, controls, forms, routing and contracts are not implemented in this release; [the design note](../design/Components030.md) lists them.
