# Projects and bundles

This page is the reference for Neura projects as implemented in 0.3.0: the manifest `neura.toml`, the lock `neura.lock`, the bundle each target builds to, and the commands that build, run, create, serve and package a project. The [project design](../design/Projects030.md) keeps the full rules, the rows not implemented yet, and the reasons.

A directory is a project when it holds `neura.toml`. Without one, every command behaves as in 0.2: a single-file `neura build app.nra -o app.nvr` is a single-file build even in a project directory.

## Creating a project

`neura new <directory> --template <name>` writes a project from a template of the SDK: `fullstack` (packages `contracts`, `ui`, `server` and `desktop`, with web, server and desktop targets), `web`, `server` or `desktop` (a web target and the desktop target that embeds it). The directory must not exist or be empty; a directory that holds anything is refused and nothing is written. The project name is the directory's name unless `--name`, and the id `example.<name>` unless `--id`. The tool then writes `neura.lock`. The templates are installed in `share/neura/templates`; the output depends only on the template, the name, the id and the SDK version. `examples/apps/stock` is `neura new stock --template fullstack` without its lock. [Your first Neura project](FirstProject.md) walks through one.

## Packages and entries

A package is a source directory, `root`, with optional `assets` directories and `depends`. Modules follow files as everywhere in Neura, and the import root of a package is its root's parent: the package with `root = "ui"` holds the module `Ui.Main` in `ui/main.nra` and `Ui.Parts.List` in `ui/parts/list.nra`. A target's `entry` names a program module (`module:program.Entry` with `main`) of the target's package; a name with no file there is [E1024](../diagnostics/E1024.md).

```toml
schema = 1

[project]
name = "counter"
id = "example.counter"
version = "1.0.0"
language = "0.3"

[packages.ui]
root = "ui"
assets = ["ui/assets"]

[packages.server]
root = "server"

[targets.web]
kind = "web"
package = "ui"
entry = "Ui.Main"
styles = ["ui/base.css"]
capabilities = ["ui"]

[targets.server]
kind = "server"
package = "server"
entry = "Server.Main"
capabilities = ["http_server", "app", "sql"]
providers = ["sqlite"]

[targets.server.sql.main]
provider = "sqlite"
migrations = "server/migrations"
migrate = "on_start"
```

The compiler is given the import roots of the target's package and of the packages it depends on. Because sibling packages share a parent, the build first follows the imports from the entry through the project's packages: an import of a module whose package is not the importer's own, not `Std` and not in its `depends` closure is [E1030](../diagnostics/E1030.md) at the import. This keeps server code out of a web bundle when `ui` does not depend on `server`.

`neura check` of a file inside a project, without `-I`, finds the nearest `neura.toml` above the file, checks the file with its package's import roots, and applies the same import rule.

## The lock

`neura.lock` records the language version, each external dependency's digest and the components each target resolves to: `browser-runtime` for a web target, `server-host` and each provider for a server target, `desktop-engine` and each provider for a desktop target. `neura build` compares it with the project and changes nothing; a missing lock or a difference is [E1022](../diagnostics/E1022.md), and `neura build --update-lock` writes it. In 0.3.0 a component's version is the SDK's version (the desktop engine's is 45.3.3), and its digest is the SHA-256 of its kind, name and version (`neura-component-v1`), because target components carry no installed metadata yet; the files a bundle holds are pinned by the bundle itself. A lock therefore names the SDK it was written with: after an SDK update, `neura build` reports `E1022` until `neura build --update-lock` records the new versions. For this reason `examples/apps/stock` is committed without its lock.

## Building a target

```sh
neura build --project neura.toml --target web
neura build --target server            # neura.toml of the current directory
neura build --target desktop --out /tmp/desktop
```

`neura build` takes a project when it has `--project`, or `--target` without `--native` (where `--target` names a native triple). The bundle is written to `dist/<target>` of the project, or to `--out`. `--update-lock`, `--no-optimize` and `--no-lift-hints` apply; `--web-runtime <directory>` names the browser runtime. `--source-map` is refused for a project in 0.3.0. The image is compiled without a source map, with the source named relative to the project, so the same sources, manifest, lock and components give the same bundle bytes from any directory.

After compiling a web or server target, the build reads the image's host imports (a desktop target's native child is not checked). A service whose capability group the target does not list, or that its kind cannot have, is [E1027](../diagnostics/E1027.md) at the `entry`: a web target that reaches `neura.fs.*`, or a server that serves requests without listing `app`. The console, clock, entropy and process exit services are always available. A provider the SDK does not define or install is [E1031](../diagnostics/E1031.md). A file over `W-1` (64 MiB unless the target sets it) is [E1029](../diagnostics/E1029.md).

| Kind | Bundle |
| --- | --- |
| `web` | `index.html`, `app.<sha256>.nvr`, `nvm.<sha256>.wasm` and `.mjs`, the page modules of the browser runtime renamed by their digests, the manifest's `styles` and the stylesheets the build listed beside the image as `styles/<sha256>.css`, and every file of the package's asset roots as `assets/<sha256>.<extension>`. `index.html` links the stylesheets in order, carries the content-security policy (`connect-src`, `img-src` and `style-src` add the manifest's `connect`, `load` and `style` origins) and checks every module by its digest. Under `routing = "history"`, `404.html` is a copy of `index.html`. |
| `server` | `app.<sha256>.nvr`, `server.json` (the `listen`, `tls_listen` and `admin_listen` addresses, the configuration prefix and each SQL pool's provider, migrations and `migrate` mode), and each pool's migrations as `migrations/<pool>/NNNN_name.sql`, which keep their names because the host orders them by number. Since 0.3.1 a server target may have `[targets.server.origins]` with only `connect`: the `http` and `https` origins its `http_client` grant is limited to, `connect = []` for none; without the table the grant reaches every origin. |
| `desktop` | The web bundle of its `ui` target, built first into `dist/<ui target>` and copied unchanged into `web/`; the native child, `bin/app-<sha256>`, built with `--native --deny-all --allow-clock` and the largest step limit, plus `--allow-http-client` for a target that declares `http_client` ([Desktop applications](Desktop.md#writing-the-native-child)), limited to the `http` and `https` origins of the target's `origins.connect` list, or of its `ui` target's when the desktop target has none, and to no origin when neither has one ([HTTP client origins](../runtime/NVM.md#http-client-services)); `registry.json` of the manifest's `bridge.operations`; and `desktop.json`, the package description the desktop host reads. |

`tests/project/browser-check.py` (`project-web-browser`, registered when the build has the browser runtime) opens the `fullstack` template's page, built by `neura build --target web` and served from a deep path with `application/wasm` and with `application/octet-stream` for `nvm.wasm`, and from `neura dev`, in headless Chromium and Firefox.

The web target needs the browser runtime: `nvm.wasm` and `nvm.mjs` beside the page modules, found in `--web-runtime`, then `NEURA_WEB_RUNTIME`, then `share/neura/web` of the installation. An SDK built with the browser runtime installs both there; otherwise the build reports `E1031` and names the directory it searched.

## bundle.json

Every bundle has `bundle.json` at its root: canonical JSON (sorted keys, no spaces, integers only) with `schema` (1), `kind`, `id`, `name`, `version`, `language`, `image` (`path` and `digest`), `files` (each `path`, `sha256`, `bytes`, `media_type` and `initial`), `requires`, `capabilities`, `origins`, `limits` and `contracts`; a web bundle adds `csp`, `routing` and `base`, a desktop bundle `ui`, the SHA-256 of the embedded web bundle's `bundle.json`, and a `neura dev` build `"dev": true`. It holds no time and no absolute path. The identity of a bundle is the SHA-256 of `bundle.json`, which `neura build` prints.

`requires` names the version of every host interface the bundle needs: `browser_adapter` and `ui_host` for a web bundle, `server_host` and `provider.<name>` for a server bundle, `desktop_host` and its providers for a desktop bundle. All are version 1 in 0.3.0. Since 0.3.1 a server or desktop bundle whose `http_client` grant has an origin list also requires `http_client_origins` 1 and lists the origins as `origins.connect`, so a host that cannot enforce the list refuses the bundle. A desktop bundle with `http_client` always has the list, empty when neither it nor its `ui` target lists `connect`. The requirement decides: a bundle that requires `http_client_origins` without an `origins.connect` list reaches no origin.

The build writes the bundle into a staging directory, verifies it as a host does, and only then moves it into place.

## Running a bundle

```sh
neura run dist/server -- --sql-pool main=sqlite:counter.db --config greeting=hi
neura run dist/desktop
```

`neura run <directory>` verifies the bundle before anything runs: every file of the directory is listed and matches its size and digest, `bundle.json` is canonical, the schema and language are known, and every `requires` axis has the host's version. An extra, missing or changed file is refused with [E1029](../diagnostics/E1029.md) naming it, an unsupported interface with [E1031](../diagnostics/E1031.md) naming the axis and both versions, and a bundle built by `neura dev` with `E1029`.

A server bundle then runs under `neura-server-host`, beside `neura-run` or on `PATH`, in the foreground: it listens on the manifest's `listen` (127.0.0.1:8080 without one) unless the arguments after `--` give `--listen`, `--tls-listen` or `--no-http`; each pool's migrations and `migrate` mode are passed as configuration, and the `fs`, `net`, `http_client` and `env` groups as the host's `--allow-*` options; `http_client` carries the bundle's `origins.connect` list when the bundle requires `http_client_origins`. The pools' URLs and the rest of the configuration are the operator's, given after `--`. The operator can withdraw a scoped grant with `--deny-http-client` but cannot widen it: `--allow-http-client` in either form after `--` is refused before the host starts, since a plain one would lift the list and a listed one would add to it.

A desktop bundle runs under the pinned Electron 45.3.3 named by `NEURA_DESKTOP_ENGINE`. The installed engine is `lib/neura/desktop/electron/electron` on Linux and `lib/neura/desktop/electron/electron.exe` on Windows. The desktop host's `main.js` loads the bundle as a package directory from `share/neura/hosts/desktop`, or from `NEURA_DESKTOP_HOST` when set. The `desktop` capability grants the clipboard services.

A web bundle is not runnable: `neura run` names `neura dev` and a static file host instead.

## Developing

```sh
neura dev --target web               # http://127.0.0.1:4310/
neura dev --target server --port 9000 -- --config greeting=hi
```

`neura dev [--project <manifest>] --target <name> [--port <n>] [-- host arguments]` builds the target into `dist/dev/<target>`, marked `"dev": true`, runs it and rebuilds when a file it reads changes, in the foreground. It writes `neura.lock` only when there is none. A watcher compares the SHA-256 of the manifest, the lock, every package's source and asset files and the targets' stylesheets, icons and migrations every 250 ms; other files of the project, such as `dist/` and logs, are not watched. A build that fails is reported and the last good bundle stays in use.

| Kind | What runs |
| --- | --- |
| `web` | A static HTTP server on `127.0.0.1`, port 4310 unless `--port` (`--port 0` lets the system choose; a busy port is an error before anything is built). The bundle carries `neura-dev.js`, which polls `/__neura/dev`: after a build the page reloads, or, when only stylesheet files changed, the links are replaced in place in their order. The state of the running program is not kept. Under history routing every application path is the page. |
| `server` | `neura-server-host` with the bundle, as `neura run` starts it; `--port` listens on `127.0.0.1:<port>`, and the arguments after `--` go to the host. It is restarted after each build. |
| `desktop` | The desktop host with the bundle, as `neura run` starts it, restarted after each build. The embedded web bundle is not marked `dev`. |

`neura dev` stops on an interrupt, a hangup (closing its terminal), a termination request, or the end of its standard input. On POSIX it stops each host group with `SIGTERM` and, after two seconds, `SIGKILL`. On Windows it sends Ctrl+Break to the host group and ends its Job Object to reap descendants. It cleans up the old host when it restarts and when it stops. `neura run` also uses a Windows Job Object for the desktop host. `neura run` and `neura package` refuse a bundle marked `dev` (`E1029`).

## Packaging

```sh
neura package --target server
neura package --target desktop --format deb
```

`neura package [--project <manifest>] --target <name> [--format <format>] [--bundle <directory>] [-o <file>]` wraps a target's built bundle, `dist/<target>` unless `--bundle`, for delivery; build it first with `neura build`. The bundle is verified as a host would before anything is copied, and a bundle built by `neura dev` is refused (`E1029`). The lock is compared as by `neura build`. The command prints the package's SHA-256, the bundle's identity, and that the package is unsigned.

A server target gives `dist/<name>-<version>-<target>.tar.gz` holding `<name>-<version>/bundle/`, `bin/neura-run`, `bin/neura-server-host`, `lib/neura/libneura-core-<version>` (which holds the SQL and TLS providers) and `run`, a shell script that runs `neura-run run bundle -- "$@"`: the bundle is verified at every start, and the arguments go to the server host. Entries are sorted, owned by 0:0 and dated at `SOURCE_DATE_EPOCH` (default 0), and the gzip stream records no name or time and stores the data uncompressed, so the same bundle and tools give the same bytes.

A desktop target gives a `.tar.gz` or `.deb` through the [desktop packager](Desktop.md#packages), with the format from `--format` or the first of the target's `packaging.formats`. It needs the target's `packaging.icon` (a 1,024-pixel PNG) and the pinned Electron 45.3.3 named by `NEURA_DESKTOP_ENGINE`. A web target is delivered as its directory and is not packaged.

`E1033` reports what cannot be built here: `.dmg` and `.msix` (built on macOS and Windows), `--sign` (Neura 0.3.0 signs nothing on Linux), a web target, a target with no built bundle, a desktop target without its icon or engine, and a server package in a format other than `.tar.gz`. `--report` is not implemented.

