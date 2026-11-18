<!-- Generated from docs/apps/Projects.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Projects and bundles

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This page is the reference for Neura projects as implemented in 0.3.0: the manifest `neura.toml`, the lock `neura.lock`, the bundle each target builds to, and the commands that build, run, create, serve and package a project. The [project design](https://github.com/Kayyo321/neura/blob/main/docs/design/Projects030.md) keeps the full rules, the rows not implemented yet, and the reasons.

A directory is a project when it holds `neura.toml`. Without one, every command behaves as in 0.2: a single-file `neura build app.nra -o app.nvr` is a single-file build even in a project directory.

## Packages and entries

A package is a source directory, `root`, with optional `assets` directories and `depends`. Modules follow files as everywhere in Neura, and the import root of a package is its root's parent: the package with `root = "ui"` holds the module `Ui.Main` in `ui/main.nra` and `Ui.Parts.List` in `ui/parts/list.nra`. A target's `entry` names a program module (`module:program.Entry` with `main`) of the target's package; a name with no file there is [E1024](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1024.md).

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

In 0.3.0 the compiler is given the import roots of the target's package and of the packages it depends on. Because sibling packages share a parent, a module can still import a package it does not depend on: the import rule `E1030` is not enforced yet.

## The lock

`neura.lock` records the language version, each external dependency's digest and the components each target resolves to: `browser-runtime` for a web target, `server-host` and each provider for a server target, `desktop-engine` and each provider for a desktop target. `neura build` compares it with the project and changes nothing; a missing lock or a difference is [E1022](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1022.md), and `neura build --update-lock` writes it. In 0.3.0 a component's digest is the SHA-256 of its kind, name and version (`neura-component-v1`), because target components carry no installed metadata yet; the files a bundle holds are pinned by the bundle itself.

## Building a target

```sh
neura build --project neura.toml --target web
neura build --target server            # neura.toml of the current directory
neura build --target desktop --out /tmp/desktop
```

`neura build` takes a project when it has `--project`, or `--target` without `--native` (where `--target` names a native triple). The bundle is written to `dist/<target>` of the project, or to `--out`. `--update-lock`, `--no-optimize` and `--no-lift-hints` apply; `--web-runtime <directory>` names the browser runtime. `--source-map` is refused for a project in 0.3.0. The image is compiled without a source map, with the source named relative to the project, so the same sources, manifest, lock and components give the same bundle bytes from any directory.

After compiling, the build reads the image's host imports. A service whose capability group the target does not list, or that its kind cannot have, is [E1027](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1027.md) at the `entry`: a web target that reaches `neura.fs.*`, or a server that serves requests without listing `app`. The console, clock, entropy and process exit services are always available. A provider the SDK does not define or install is [E1031](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1031.md). A file over `W-1` (64 MiB unless the target sets it) is [E1029](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1029.md).

| Kind | Bundle |
| --- | --- |
| `web` | `index.html`, `app.<sha256>.nvr`, `nvm.<sha256>.wasm` and `.mjs`, the page modules of the browser runtime renamed by their digests, the manifest's `styles` and the stylesheets the build listed beside the image as `styles/<sha256>.css`, and every file of the package's asset roots as `assets/<sha256>.<extension>`. `index.html` links the stylesheets in order, carries the content-security policy (`connect-src`, `img-src` and `style-src` add the manifest's `connect`, `load` and `style` origins) and checks every module by its digest. Under `routing = "history"`, `404.html` is a copy of `index.html`. |
| `server` | `app.<sha256>.nvr`, `server.json` (the `listen`, `tls_listen` and `admin_listen` addresses, the configuration prefix and each SQL pool's provider, migrations and `migrate` mode), and each pool's migrations as `migrations/<pool>/NNNN_name.sql`, which keep their names because the host orders them by number. |
| `desktop` | The web bundle of its `ui` target, built first into `dist/<ui target>` and copied unchanged into `web/`; the native child, `bin/app-<sha256>`, built with `--native --deny-all --allow-clock` and the largest step limit ([Desktop applications](Apps-Desktop#writing-the-native-child)); `registry.json` of the manifest's `bridge.operations`; and `desktop.json`, the package description the desktop host reads. |

The web target needs the browser runtime: `nvm.wasm` and `nvm.mjs` beside the page modules, found in `--web-runtime`, then `NEURA_WEB_RUNTIME`, then `share/neura/web` of the installation. An SDK built with the browser runtime installs both there; otherwise the build reports `E1031` and names the directory it searched.

## bundle.json

Every bundle has `bundle.json` at its root: canonical JSON (sorted keys, no spaces, integers only) with `schema` (1), `kind`, `id`, `name`, `version`, `language`, `image` (`path` and `digest`), `files` (each `path`, `sha256`, `bytes`, `media_type` and `initial`), `requires`, `capabilities`, `origins`, `limits` and `contracts`; a web bundle adds `csp`, `routing` and `base`, a desktop bundle `ui`, the SHA-256 of the embedded web bundle's `bundle.json`, and a `neura dev` build `"dev": true`. It holds no time and no absolute path. The identity of a bundle is the SHA-256 of `bundle.json`, which `neura build` prints.

`requires` names the version of every host interface the bundle needs: `browser_adapter` and `ui_host` for a web bundle, `server_host` and `provider.<name>` for a server bundle, `desktop_host` and its providers for a desktop bundle. All are version 1 in 0.3.0.

The build writes the bundle into a staging directory, verifies it as a host does, and only then moves it into place.

## Running a bundle

```sh
neura run dist/server -- --sql-pool main=sqlite:counter.db --config greeting=hi
neura run dist/desktop
```

`neura run <directory>` verifies the bundle before anything runs: every file of the directory is listed and matches its size and digest, `bundle.json` is canonical, the schema and language are known, and every `requires` axis has the host's version. An extra, missing or changed file is refused with [E1029](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1029.md) naming it, an unsupported interface with [E1031](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1031.md) naming the axis and both versions, and a bundle built by `neura dev` with `E1029`.

A server bundle then runs under `neura-server-host`, beside `neura-run` or on `PATH`, in the foreground: it listens on the manifest's `listen` (127.0.0.1:8080 without one) unless the arguments after `--` give `--listen`, `--tls-listen` or `--no-http`; each pool's migrations and `migrate` mode are passed as configuration, and the `fs`, `net` and `env` groups as the host's `--allow-*` options. The pools' URLs and the rest of the configuration are the operator's, given after `--`.

A desktop bundle runs under the pinned Electron 45.3.3 named by `NEURA_DESKTOP_ENGINE` (or installed as `lib/neura/desktop/electron/electron`) with the desktop host's `main.js` from `share/neura/desktop` (or `NEURA_DESKTOP_HOST`), which loads the bundle as a package directory. The `desktop` capability grants the clipboard services.

A web bundle is not runnable: `neura run` names `neura dev` and a static file host instead.
