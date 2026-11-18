# Your first Neura web app

This walkthrough builds a page whose user interface is written in Neura, bundles it as static files, and serves it. The page has a heading and a counter component, styled by a stylesheet; a click renders the counter alone. Everything here runs in Neura 0.3.0 as it is on this branch, and [the browser check](../../tests/apps/web/app-check.py) runs the same machinery in headless Chromium and Firefox. [State and components](Components.md) explains the library, [HTML factories](Html.md) the templates, and [Stylesheets](Css.md) the `css` forms.

*Interim:* until the project tools exist ([Projects](../design/Projects030.md)), the bundle is assembled by `scripts/build-web-bundle.py` rather than by `neura build` of a `web` target, and the browser runtime is built from source with the pinned Emscripten.

## 1. Build the compiler and the browser runtime

```sh
git submodule update --init --recursive --depth 1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3
NEURA_EMSDK=/path/to/emsdk cmake --preset wasm-runtime -DNEURA_WASM_HOST=ON
cmake --build build/wasm-runtime --target neura_wasm
```

The browser runtime needs the Emscripten SDK pinned in `packaging/components.lock.toml` (version 7.0.0); the configure step checks it with `scripts/check-emsdk.py`. The second build writes `build/wasm-runtime/nvm.wasm` and `nvm.mjs`.

## 2. Write the application

Save this as `app.nra`. It is [`examples/apps/web-counter/app.nra`](../../examples/apps/web-counter/app.nra).

<!-- example: check -->
```neura
// Your first Neura web app (docs/apps/WebApp.md): a page with a heading and a counter component, styled by a
// stylesheet and mounted on the element #app. A click renders the counter alone; the heading is not rendered again.
let console = import:Std.Io.Console;
let error = import:Std.Core.Error;
let program = import:Std.Program;
let result = import:Std.Core.Result;
let ui = import:Std.Ui;
let web = import:Std.Web;

const PageStyles = css {
    .page { font-family: system-ui, sans-serif; max-width: 30rem; margin: 3rem auto; }
    .count { font-size: 2rem; color: #2a5db0; }
};

let counter_view = html(count: int32, on_add: func(ui.ClickEvent) -> void) {
    <p>
        <span class="count" id="count">{count}</span>
        <button type="button" id="add" on:click={on_add}>Add one</button>
    </p>
};

let page_view = html(title: string, counter: ui.View) {
    <main class="page">
        <h1>{title}</h1>
        {counter}
    </main>
};

let make_counter = func -> ui.Component {
    last count = new ui.State<int32>(0);
    last add = new func(event: ui.ClickEvent) {
        count.update(func(value: int32) -> int32 => value + 1);
    };
    return ui.component<int32>(count, new func(value: int32) -> ui.View => counter_view(value, add));
};

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        last counter = make_counter();
        last title = new ui.State<string>("My first Neura app");
        last page = ui.component<string>(title, new func(value: string) -> ui.View =>
            page_view(value, ui.slot(counter)));
        let status = program.ExitCode.Ok;
        scope (tasks) {
            match (web.mount("#app", page, tasks)) {
                result.Result.Ok(root) => {
                    root.run();
                },
                result.Result.Err(problem) => {
                    console.print_line("mount failed: " + error.message(problem));
                    status = program.ExitCode.Failure;
                },
            }
        }
        return status;
    };
);
```

- `css { ... }` is a stylesheet. The build writes it to `styles/<sha256>.css` and the page links it; nothing registers it at run time.
- `html(...) { ... }` declares a factory: a function that returns a `ui.View`. `{count}` is a text hole and `on:click={on_add}` binds a handler that receives a `ui.ClickEvent`.
- `ui.State<int32>` holds the counter's value. A handler changes it with `update`; the component's `render` runs again at the end of the turn.
- `ui.component<T>(state, render)` ties a state to a render function. `ui.slot(counter)` embeds the counter in the page, so a click renders and commits the counter alone and the heading's nodes are not touched.
- `web.mount("#app", page, tasks)` opens a root on the element `#app` and commits the first render. `root.run()` is the event loop; it returns when the root is closed. `tasks` is the handle of the enclosing `scope`.

Check it with `build/neura check app.nra`. Run natively, the program prints `mount failed: ... the ui capability is not granted: run the image in a browser page (14)`: only a browser page serves the `ui` services.

## 3. Bundle it

```sh
python3 scripts/build-web-bundle.py --source app.nra --neura build/neura \
    --id org.example.counter --title "My first Neura app" --out dist
```

The script runs `neura build`, then writes `dist/`: `index.html` with a content-security policy, the stylesheet links in the build's order and the mount element `<div id="app">`; the image and `nvm.wasm` named by their SHA-256; the browser host modules; `styles/` and `assets/` from the build; and `bundle.json`, which lists every file. [Host providers](../runtime/Providers.md#web-bundles) describes the layout. `--image app.nvr` bundles an image you built yourself and reads `app.nvr.styles.json` beside it.

## 4. Serve it

```sh
python3 -m http.server --directory dist 8000
```

Open `http://localhost:8000/`. The page shows the heading and `0`; each click on *Add one* increments the number. The bundle uses relative references only, so any static file host serves it under any path: copy `dist/` to the host. A browser without WebAssembly exception handling shows a message instead of the application.

The bundle uses hash routing by default: a router keeps its path in the fragment (`/#/parts/7`), which every static host serves. For addresses without the fragment, bundle with `--routing history` and, when the application does not live at the site's root, `--base /app/`. The page then references its files from the base, and the server must follow one rule, which the script prints: serve the bundle's files at `<base><file>` and answer every other path below the base with `index.html`, so that a deep link such as `/app/parts/7` loads the application at that route. The script also writes `404.html`, a copy of `index.html`, for hosts that serve that file for unknown paths. The same image runs under either mode: the router asks the page for its links.

## Going further

[`tests/apps/web/app/app.nra`](../../tests/apps/web/app/app.nra) is a larger page checked in the browsers: a keyed `@for` list that adds, removes and reverses items over a `ui.State<FrozenList<string>>`, a text input bound both ways with `prop:value` and `on:input`, and a button that closes the root from a handler. Its check confirms after each step that only the component that changed renders and commits, that reordered items keep their elements, and that typing keeps the input's focus and selection.

Not in 0.3.0 yet: commands for work that waits, the router, controls and forms with validation, contracts, `neura build` of a `web` target, and the render-effect check `E1010`. [The design note](../design/Components030.md) lists what remains.
