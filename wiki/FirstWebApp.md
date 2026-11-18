<!-- Generated from docs/apps/WebApp.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Your first Neura web app

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This walkthrough builds a page whose user interface is written in Neura, bundles it as static files, and serves it. The page has a heading and a counter component, styled by a stylesheet; a click renders the counter alone. Everything here runs in Neura 0.3.0 as it is on this branch, and [the browser check](https://github.com/Kayyo321/neura/blob/main/tests/apps/web/app-check.py) runs the same machinery in headless Chromium and Firefox. [State and components](StateAndComponents) explains the library, [HTML factories](HtmlFactories) the templates, and [Stylesheets](Stylesheets) the `css` forms.

*Interim:* until the project tools exist ([Projects](https://github.com/Kayyo321/neura/blob/main/docs/design/Projects030.md)), the bundle is assembled by `scripts/build-web-bundle.py` rather than by `neura build` of a `web` target, and the browser runtime is built from source with the pinned Emscripten.

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

Save this as `app.nra`. It is [`examples/apps/web-counter/app.nra`](https://github.com/Kayyo321/neura/blob/main/examples/apps/web-counter/app.nra).

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

The script runs `neura build`, then writes `dist/`: `index.html` with a content-security policy, the stylesheet links in the build's order and the mount element `<div id="app">`; the image and `nvm.wasm` named by their SHA-256; the browser host modules; `styles/` and `assets/` from the build; and `bundle.json`, which lists every file. [Host providers](Providers#web-bundles) describes the layout. `--image app.nvr` bundles an image you built yourself and reads `app.nvr.styles.json` beside it.

## 4. Serve it

```sh
python3 -m http.server --directory dist 8000
```

Open `http://localhost:8000/`. The page shows the heading and `0`; each click on *Add one* increments the number. The bundle uses relative references only, so any static file host serves it under any path: copy `dist/` to the host. A browser without WebAssembly exception handling shows a message instead of the application.

The bundle uses hash routing by default: a router keeps its path in the fragment (`/#/parts/7`), which every static host serves. For addresses without the fragment, bundle with `--routing history` and, when the application does not live at the site's root, `--base /app/`. The page then references its files from the base, and the server must follow one rule, which the script prints: serve the bundle's files at `<base><file>` and answer every other path below the base with `index.html`, so that a deep link such as `/app/parts/7` loads the application at that route. The script also writes `404.html`, a copy of `index.html`, for hosts that serve that file for unknown paths. The same image runs under either mode: the router asks the page for its links.

## 5. Call a Neura backend

A page asks a server for data with `Std.Net.Http.Fetch` ([HTTP fetch](Providers#http-fetch)). [`examples/apps/web-backend/app.nra`](https://github.com/Kayyo321/neura/blob/main/examples/apps/web-backend/app.nra) is a page that asks the server image `examples/server/hello-server.nra` for a greeting (`GET /api/hello?name=Ada`) and sends it a message to echo (`POST /api/echo`). Each call runs as a command of [`Std.Ui.Commands`](StateAndComponents#commands), so the page keeps taking input while it waits; the status line shows `Waiting for the server`, then the server's answer or the failure. The work of the command is one `send`:

```text
let request = http.request(call.method, call.path);
if (call.body != "") {
    request = request.with_text("text/plain; charset=utf-8", call.body);
}
match (request.send()) { ... }
```

Run the server with `neura-server-host`, built beside `neura`, which runs the image once for each request (its page is `docs/runtime/ServerHost.md`):

```sh
build/neura build examples/server/hello-server.nra -o hello-server.nvr
build/neura-server-host --listen 127.0.0.1:8080 --config greeting=hello hello-server.nvr
```

Bundle the page with `--http-client`, which grants `neura.http.fetch` for the page's own origin; without it every call answers error 14 and the script says so:

```sh
python3 scripts/build-web-bundle.py --source examples/apps/web-backend/app.nra --neura build/neura \
    --id org.example.backend --title "Ask the server" --http-client --out dist
```

Serve both under one origin: a reverse proxy serves `dist/` and passes every path below `/api/` to the server host without the prefix, so `/api/hello` reaches the server as `/hello`. With nginx, for example, that is `location /api/ { proxy_pass http://127.0.0.1:8080/; }` beside `root dist;`. One origin needs no CORS headers and no `--connect`. A backend on another origin is listed with `--connect https://api.example.com` and must answer with CORS headers, which `hello-server.nra` does not send ([HTTP fetch](Providers#http-fetch)).

`tests/apps/web/backend-check.py` (`app-web-backend`, skipped when `neura-server-host` was not built beside `neura`) runs this in headless Chromium and Firefox with a small proxy of that shape: it greets twice, sends `ping é ✓`, and checks the server's answers on the page and the requests the server received; then the proxy answers 502, as it does when the server is down, and the page shows `Failed: the server answered 502`. A native `neura run` of the page does not start: it does not serve `neura.http.fetch` yet, so the image fails to bind.

## Going further

[`tests/apps/web/app/app.nra`](https://github.com/Kayyo321/neura/blob/main/tests/apps/web/app/app.nra) is a larger page checked in the browsers: a keyed `@for` list that adds, removes and reverses items over a `ui.State<FrozenList<string>>`, a text input bound both ways with `prop:value` and `on:input`, and a button that closes the root from a handler. Its check confirms after each step that only the component that changed renders and commits, that reordered items keep their elements, and that typing keeps the input's focus and selection.

Not in 0.3.0 yet: contracts, `neura build` of a `web` target (`scripts/build-web-bundle.py` stands in), and the render-effect check `E1010` for the render function of a `ui.component` (templates have it). [The design note](https://github.com/Kayyo321/neura/blob/main/docs/design/Components030.md) lists what remains.
