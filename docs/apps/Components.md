# State and components

`Std.Ui` keeps application state in `ui.State<T>` values, renders it with components built by `ui.component`, and runs a root's event loop over a host: a browser document, a desktop window, or the model host that tests use. A component's view comes from an [HTML factory](Html.md). `Std.Ui.Commands` runs work that waits, `Std.Ui.Routing` maps paths to components, and `Std.Ui.Forms` and `Std.Ui.Controls` provide validated fields and controls over the platform's own elements. This page states what Neura 0.3.0 implements; [the design note](../design/Components030.md) holds the full contract and the rows that remain open.

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
| A child that a render embeds is mounted during the turn, and a child that a render drops is unmounted only when the host commits. A refused or failed commit rolls the turn back: the children it mounted are unmounted again (their generation rises), the children it dropped stay mounted with their subscriptions, and each component that rendered keeps its previous view. Those components stay clean: they render again when their state next notifies, so one refused render does not block the rest of the root. | implemented |
| Unmounting a component, by a parent's render or by `root.close()`, unmounts its children, closes its subscription, cancels the commands bound to it, and raises its `generation()`. An event queued for a component that has since unmounted is discarded and counted. | implemented |

## Events

A host copies an event's fields into an immutable record (`ui.ClickEvent`, `ui.InputEvent`, `ui.PointerEvent` and the others in [HTML factories](Html.md)) and posts it to the root with `root.post(listener, component, coalesce, deliver)`. No browser object reaches Neura.

| Rule | Status |
| --- | --- |
| Events are delivered in arrival order, one per turn. A coalesced event (pointer moves, scrolls) replaces the queued event of the same listener and keeps its place. | implemented in the root; the browser adapter that posts them is not |
| At the queue limit (1,024 by default, `set_queue_limit` up to 65,536) the root is overloaded: an arriving event that cannot be coalesced is not queued and is counted. Before the next event the root accepts, it delivers `ui.RootEvent.InputOverflow(dropped)` to the `on_overflow` handler and asks the host to resynchronize, so that the host posts one input event for each controlled element whose value changed. | implemented in the root and the model host |

## Commands

A handler should finish within its turn. Work that waits, such as a call to a service, is a `commands.Command<I, O>` of `Std.Ui.Commands`: `command.run(input)` starts `work(input)` as a task, and a later turn applies its result to `command.state`, a `ui.State<commands.CommandState<O>>` that components render.

<!-- example: run -->
```neura
let commands = import:Std.Ui.Commands;
let console = import:Std.Io.Console;
let error = import:Std.Core.Error;
let model = import:Std.Ui.ModelHost;
let program = import:Std.Program;
let result = import:Std.Core.Result;
let ui = import:Std.Ui;

let status_view = html(text: string, on_load: func(ui.ClickEvent) -> void) {
    <p role="status">{text}</p>
    <button type="button" on:click={on_load}>Load</button>
};

// Task-safe work with a transferable input and result.
let count_parts = func(shelf: string) -> result.Result<int32> {
    if (shelf == "") {
        return result.Result<int32>.Err(error.custom("parts.shelf", "no shelf given"));
    }
    return result.Result<int32>.Ok(shelf.byte_length as int32);
};

let describe = func(state: commands.CommandState<int32>) -> string => match (state) {
    commands.CommandState.Idle => "Not loaded",
    commands.CommandState.Running => "Loading",
    commands.CommandState.Succeeded(count) => count.to_string() + " parts",
    commands.CommandState.Failed(problem) => "Failed: " + error.message(error.from_portable(problem)),
    commands.CommandState.Cancelled => "Cancelled",
};

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let load = new commands.Command<string, int32>(count_parts);
        let on_load = new func(event: ui.ClickEvent) {
            load.run("bolts");
        };
        let status = ui.component<commands.CommandState<int32>>(load.state,
            new func(state: commands.CommandState<int32>) -> ui.View {
                console.print_line("render " + describe(state));
                return status_view(describe(state), on_load);
            });
        load.bind(status);
        let host = new model.Host();
        let root = result.expect<ui.Root>(ui.mount(host, "#app", status), "mount");
        host.click(1, status, on_load);
        host.await_completion();
        commands.run(root);
        return program.ExitCode.Ok;
    };
);
```

```text
render Not loaded
render Loading
render 5 parts
```

| Rule | Status |
| --- | --- |
| `new commands.Command<I, O>(work)` takes `work: func(I) -> Result<O>`. The work runs as a task, so it must be task-safe and its captures, `I` and `O` transferable: a work function that captures a `ui.State` is `E0602`, reported at the program's call of `commands.run` with a label at the capture. Its `Err` reaches the state as `Failed` with the error's `PortableError`. The check is on the work's function type over the whole program, so another closure of the same type that captures a state, such as a `func(string) -> Result<string>` parser of a form field next to a `Command<string, string>`, makes the command's work non-transferable too; give the work a type of its own, for example by wrapping its input in a small `#Immutable` class. | implemented |
| `command.bind(component)` makes the component the command's owner. `command.run(input)` returns `commands.RunOutcome.Started` and sets the state to `Running`, or `Rejected` with nothing started when the command has no mounted owner or the owner's root has `U-10` runs in flight (32 by default; `root.set_command_limit` up to 1,024). | implemented |
| Each `run`, each `cancel()` of a running command and each unmount of the owner raise `command.generation()`. A completion of an older generation is discarded and counted in `root.stale_completions()`: a command run twice quickly keeps the second result, and one that completes after its owner unmounted, or after a remount, changes nothing. `cancel()` with no run in flight does nothing. | implemented |
| A run works in a scope of its own: `cancel()` and the owner's unmount cancel that scope, and the work learns of it through `Std.Concurrent.Cancellation.requested()` or a parked operation that returns `Cancelled`. There is no cancellation token argument. Nothing is detached, and no `Scope` is stored. | implemented |
| `commands.run(root)` is the root's event loop with the scope that owns every command task: each turn is the next event or one command completion. With commands in flight and no event queued it waits for both together when the host is a `ui.TaskHost`, and otherwise for the next completion first. It returns when the root is closed or faulted, or when the host has no more events and no run is in flight; it cancels the runs still in flight and returns after their tasks have finished. `root.run()` traps with `a command needs Std.Ui.Commands.run(root)` when a command was started. | implemented |
| `Std.Ui.Commands` is a module of its own because it uses tasks: a program that uses it cannot also use `Std.DynamicForeign` or a C import ([Concurrency](../lang/Concurrency.md)), and a program that uses only `Std.Ui` still can. | implemented |

## Routing

`Std.Ui.Routing` maps path patterns to components. A pattern is an absolute path whose segments are literal text or a parameter `{name}`: `/`, `/parts`, `/parts/{id}`. The router is a component that embeds the component of the route its location matches.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let model = import:Std.Ui.ModelHost;
let program = import:Std.Program;
let result = import:Std.Core.Result;
let routing = import:Std.Ui.Routing;
let ui = import:Std.Ui;

let page_view = html(title: string) {
    <h1>{title}</h1>
};

let page = func(title: string) -> ui.Component =>
    ui.component<string>(new ui.State<string>(title), new func(text: string) -> ui.View {
        console.print_line("render " + text);
        return page_view(text);
    });

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let router = new routing.Router();
        router.add("/", new func(params: routing.Params) -> ui.Component => page("Home"));
        router.add("/parts/{id}", new func(params: routing.Params) -> ui.Component =>
            page("Part " + params.get("id")));
        let host = new model.Host();
        host.start_at("/parts/7");
        let root = result.expect<ui.Root>(ui.mount(host, "#app", router.component()), "mount");
        console.print_line(router.href("/"));
        host.click(1, router.component(), router.on_link("/"));
        host.end_batch();
        host.back();
        host.end_batch();
        root.run();
        return program.ExitCode.Ok;
    };
);
```

```text
render Part 7
#/
render Home
render Part 7
```

| Rule | Status |
| --- | --- |
| `router.add(pattern, make)` adds a route whose component `make(params)` builds. A literal segment has no space, control character, `{`, `}`, `*`, `?`, `#` or `%`; parameter names are distinct identifiers. An invalid pattern, or one with the same shape as an earlier route (`/{part}` after `/{id}`), traps when it is added. `router.otherwise(make)` sets the component for a path no route matches (by default an empty, hidden element). | implemented |
| A path matches a route with as many segments, equal literals and non-empty parameters. A literal segment wins over a parameter at the first position where two matching routes differ, so `/parts/new` beats `/parts/{id}`. A trailing slash is ignored, the query after `?` is kept in `location.query`, a fragment is dropped, and a parameter value is percent-decoded (an invalid escape or invalid UTF-8 matches nothing). `router.resolve(path)` returns the `routing.Location` with the route and its `routing.Params`. | implemented |
| `router.component()` renders the current `router.location`, a `ui.State<routing.Location>`. When the location changes it embeds a new component for the new route, and the root unmounts the old one; returning to a path builds its component again. | implemented |
| Mounted on a host that is a `ui.HistoryHost`, the router starts at the host's location (a deep link), `navigate(path)` pushes a history entry, `replace(path)` replaces the current one, and a navigation the program did not make (back, forward, an edited address) moves it in a turn. Navigations the host posts while a turn is pending coalesce to the latest, and the queue limit never drops one. Without such a host, `navigate` only changes the location. | implemented on the model host |
| `router.link(path)` is the `ui.Url` for an `href` in the host's routing mode (`#/parts/7` for hash routing, the default, or `/parts/7` for history routing), and `router.on_link(path)` is a click handler that navigates, bound with `on:click.prevent_default`. A path is absolute, at most 8 KiB, without control characters; another traps. | implemented; the browser adapter that maps the mode to `location` is the browser host's |

## Forms and controls

`Std.Ui.Forms` pairs each input's draft text with the function that parses it, and `Std.Ui.Controls` renders fields and other controls as the platform's own elements, so focus, keyboard use and accessibility come from the browser. The [portfolio example](../../examples/apps/portfolio.nra) combines a router and a contact form.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let controls = import:Std.Ui.Controls;
let forms = import:Std.Ui.Forms;
let model = import:Std.Ui.ModelHost;
let program = import:Std.Program;
let result = import:Std.Core.Result;
let ui = import:Std.Ui;

let order_view = html(name: ui.View, count: ui.View, send: ui.View) {
    {name}{count}{send}
};

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let name = new forms.Field<string>("name", "Name", "", forms.required_text("Enter a name"));
        let count = new forms.Field<int64>("count", "Count", "1", forms.integer(1, 99, "Enter 1 to 99"));
        let order = new forms.Form();
        order.add(name);
        order.add(count);
        let name_input = controls.text_input<string>(name, "text");
        let count_input = controls.text_input<int64>(count, "number");
        let send = new func() {
            console.print_line("ordered " + name.draft());
        };
        let top = controls.form(order, send, new func(state: forms.FormState) -> ui.View =>
            order_view(ui.slot(name_input), ui.slot(count_input), controls.submit_button("Order")));
        let host = new model.Host();
        let root = result.expect<ui.Root>(ui.mount(host, "#order", top), "mount");
        host.submit(1, top, order.on_submit(send));
        host.end_batch();
        host.input(2, name_input, name.on_input(), "bolts");
        host.submit(1, top, order.on_submit(send));
        host.end_batch();
        root.run();
        console.print_line("focused " + host.focused);
        console.print_line(order.state.get().to_string());
        return program.ExitCode.Ok;
    };
);
```

```text
ordered bolts
focused name
FormState(2 attempts, 1 submitted, invalid '')
```

| Rule | Status |
| --- | --- |
| `new forms.Field<T>(id, label, initial, parse)` keeps its draft in `field.state`, a `ui.State<forms.FieldState>` with the draft, whether the field is `touched` (left, or the form was submitted), and the message of the parse error when the draft is not valid; `shows_error()` is touched and not valid. `on_input()`, `on_change()` and `on_blur()` are the handlers for its element; `value()` parses the draft. An `id` has letters, digits, `-` and `_` and starts with a letter. | implemented |
| `forms.Form` collects fields in order. `submit(action)`, and the handler `on_submit(action)` for `on:submit.prevent_default`, validate every field, so each shows its error, then call `action` when all are valid, or else ask the root of the form's component to focus the first invalid field once the turn has committed (`root.request_focus`, carried out by a `ui.FocusHost`). `form.state` counts attempts and submits and names the first invalid field. | implemented on the model host |
| The parsers `forms.any_text()`, `forms.required_text(message)`, `forms.integer(min, max, message)` (decimal, the whole `int64` range) and `forms.email(message)` (the shape `local@domain.tld` only) are ordinary functions; validation shared with a server is an ordinary function of a shared package and never replaces the server's own. | implemented |
| `controls.text_input(field, kind)` (`text`, `email`, `password`, `search`, `tel`, `url` or `number`) and `controls.text_area(field)` are a `label` and an `input` or `textarea` with `prop:value`, `aria-invalid` and, when the error shows, an alert linked by `aria-describedby`. `controls.select(field, choices)`, `controls.checkbox(id, label, state)`, `controls.list(items, key, item)` (a keyed `ul`), `controls.table(caption, columns, rows)` and `controls.dialog(id, title, open, content)` (a non-modal `dialog` with a Close button) are components over their state; `controls.button` and `controls.submit_button` are views; `controls.form(form, action, content)` is the `form` element. Each renders alone when its own state changes. The markup is unstyled. | implemented and tested on the model host, which checks state, events and focus requests only: no test has yet rendered this markup into a document, and focus, selection and composition in real browsers are not qualified |

## The host interface

`ui.Host` is the interface a root uses: `open(target)`, `commit(root, batch)`, `wait(root)` (post the events that arrive; false when none will), `resynchronize(root)`, `reset(root)` and `close(root)`. A `ui.Commit` lists the `ui.Rendered` component and view pairs of one turn, parents before children. Three optional interfaces add services a root uses when its host implements them:

| Interface | Methods | Used by |
| --- | --- | --- |
| `ui.TaskHost` | `wait_with(root, completions)`: post events, or return a completion ticket received from `completions`, whichever comes first (`ui.Wake.Events`, `Completed(ticket)`), or `Idle` when nothing will be posted until a command completes. A host may race a cancellable host request against the channel in a scope of its own. | `commands.run` |
| `ui.HistoryHost` | `location()`, `push_location(path)`, `replace_location(path)`, `link(path)`; the host reports other navigations with `root.post_navigation(path)` | the router |
| `ui.FocusHost` | `focus(root, element_id)` | `root.request_focus`, forms |

`Std.Ui.ModelHost` implements all four in memory for tests: a test scripts clicks, input, change, submit, blur and pointer moves in batches, each `wait` posts the next batch, `await_completion()` makes a commands loop wait for a completion, `start_at(path)`, `back()`, `forward()` and `navigate_externally(path)` script the history, `focused` records the focus, and every request is recorded in `log`. `fail_next_commit()` and `refuse_next_commit(kind)` exercise render failures, `clear_log()` forgets the recorded lines, and the handle of a closed root is reused. On it, 10,000 mount and unmount cycles with 100,000 clicks, each a turn that updates, renders and commits, leave no open subscription and a peak heap within 32 cells (longer decimal counters in the log) of the 100-cycle run (`tests/apps/ui/retained.py`, the model-host half of budget B-W9 in [the application platform](../design/Apps030.md)). The model host does not interpret views; reconciliation against a document belongs to the browser and desktop hosts.

The model-host figures of budget B-W8 are recorded in `tests/benchmarks/baseline.txt`: `ui-counter` (clicks on a counter component, each a turn that updates, renders and commits; about 890 steps a click) and `ui-list` (a keyed list control of 1,000 rows, each click replacing one row and rendering the list again; about 204,000 steps a change). They cover the Neura side of a turn only, without reconciliation or a document, so they are not evidence for the browser budgets B-W5 and B-W7. The list's peak heap rises slowly with the number of changes (98,250 cells after 10, 104,755 after about 490, 107,346 after 1,500), much less than one render allocates, and a run of 400 renders of an unchanged list stays under a cap of 115,000 cells: the growth follows the collector's schedule and is not retention of each render, but no test bounds it for lists yet.

`Std.Web.mount(selector, component, tasks)` is declared with its final signature and traps as unsupported: the browser runtime has no `neura.ui.*` host services yet. Desktop mounting, gesture actions and contracts are not implemented in this release; [the design note](../design/Components030.md) lists them.
