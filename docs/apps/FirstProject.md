# Your first Neura project

This walkthrough creates a project with a web page, a server and a desktop application that share one contract, builds each target into a bundle, and runs them. Everything here runs in Neura 0.3.0 as it is on this branch. The reference for each step is [Projects and bundles](Projects.md).

## 1. Create the project

```sh
neura new stock --template fullstack
cd stock
```

`neura new` writes a project from a template of the SDK into a directory that does not exist or is empty, and writes `neura.lock`. The templates are `fullstack`, `web`, `server` and `desktop`. The project's name is the directory's name (or `--name`), and its id is `example.<name>` (or `--id`), the reverse-domain id that names the desktop application and its data directory. Nothing is fetched: the output depends only on the template, the name, the id and the SDK.

The fullstack project has four packages:

```text
stock/
    neura.toml          the manifest: packages and targets
    neura.lock          the components each target resolves to
    contracts/parts.nra Contracts.Parts: the starting parts and the rule for a part's name
    ui/main.nra         Ui.Main: the page, mounted on #app
    ui/base.css         the page's global stylesheet
    server/main.nra     Server.Main: GET /parts and POST /parts?name=...
    desktop/main.nra    Desktop.Main: the window and the native operation parts.export
```

A package's modules are named from its directory: `ui/main.nra` is `Ui.Main`, and the `ui`, `server` and `desktop` packages each import `Contracts.Parts`, because they list `contracts` in `depends`. The same `valid_name` rule runs in the server and anywhere else that imports it.

## 2. Build the server and run it

```sh
neura build --target server
neura run dist/server
```

`neura build --target server` compiles `Server.Main` with its package's import roots, checks that the image uses only the capability groups the target lists, and writes the bundle to `dist/server`: the image named by its SHA-256, `server.json` with the host settings of the manifest, and `bundle.json`, which lists every file with its digest. It prints the bundle's identity. `neura run dist/server` checks every file against `bundle.json` and then serves the image with `neura-server-host` on the manifest's `listen` address, `127.0.0.1:8080`:

```sh
curl http://127.0.0.1:8080/parts
curl -X POST 'http://127.0.0.1:8080/parts?name=Gear'
```

The first answers the three starting parts, one per line; the second `accepted: Gear`. An empty name is answered with status 422 by the shared rule. Arguments after `--` go to the host: `neura run dist/server -- --listen 127.0.0.1:9000`.

## 3. Build the web page

```sh
neura build --target web
```

The web bundle in `dist/web` is static files: `index.html`, the image, the browser runtime and the stylesheets, each named by its digest, with the page's content-security policy in `index.html` and in `bundle.json`. Copy the directory to any static file host. `neura run dist/web` refuses, because a page runs in a browser, not under a host. The build needs the browser runtime (`nvm.wasm` and `nvm.mjs`), which an SDK built with it installs in `share/neura/web`; without it, the build reports `E1031` and names the directory it searched ([Your first Neura web app](WebApp.md#1-build-the-compiler-and-the-browser-runtime) builds it from source).

## 4. Build and run the desktop application

```sh
neura build --target desktop
NEURA_DESKTOP_ENGINE=/path/to/electron neura run dist/desktop
```

A desktop target builds its web target first and embeds the web bundle unchanged, with the same digest, beside the native child that `Desktop.Main` compiles to, `registry.json` and `desktop.json`. `neura run dist/desktop` starts the pinned Electron 45.3.3 with the desktop host ([Desktop applications](Desktop.md)).

## 5. Develop with neura dev

```sh
neura dev --target web
```

`neura dev` builds the page into `dist/dev/web`, serves it at `http://127.0.0.1:4310/` and keeps watching the project. Edit `ui/main.nra` and the open page reloads after the build; edit `ui/base.css` alone and the stylesheet is replaced without a reload. `neura dev --target server` runs the server the same way and restarts it after each change. Stop it with Ctrl-C or Ctrl-D: it stops everything it started.

## 6. Change something and build again

Edit `contracts/parts.nra` to add a starting part, and build the server again: only `dist/server` changes, and the same sources give the same bytes on every build and in every directory. If `neura build` reports `E1022`, the lock no longer matches the manifest or the SDK (a provider was added, or the SDK was updated); run `neura build --target <name> --update-lock` once and commit the new `neura.lock`.
