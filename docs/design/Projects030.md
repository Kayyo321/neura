# Neura 0.3.0: projects, bundles and application tools

This note fixes the project system before it is implemented: the project manifest `neura.toml`, the lock file `neura.lock`, the bundle a target builds to, and the commands `neura new`, `neura contracts`, `neura build --target`, `neura dev`, `neura package` and `neura run` of a bundle. It also defines the reference application and the integration milestone that qualifies the whole platform. It is one of the notes of [the application platform](Apps030.md), which owns the limits, budgets and code ranges referred to here. Rows move to `docs/apps/Projects.md` and [Tooling](../lang/Tooling.md) in the change that implements them.

The owner approved the A0 design decisions on 2026-11-17. Rows marked *proposed* await implementation and qualification in A6. Later changes to a row are made here, with the reason.

The same TOML reader handles the manifest and lock file. Single-file `neura build`, `neura run` and `neura test` keep working exactly as they do. A project is something a directory opts into by containing `neura.toml`.

```text
stock/
    neura.toml
    neura.lock
    contracts/      records, replies, interfaces, shared validation, contracts.toml
    ui/             components, templates, stylesheets, assets
    server/         routes, services, repositories, migrations
    desktop/        windows and native operations
    tests/
    dist/           build output, one directory per target (not committed)
```

## The project manifest

```toml
schema = 1

[project]
name = "stock"
id = "example.stock"
version = "1.0.0"
language = "0.3"

[packages.contracts]
root = "contracts"

[packages.ui]
root = "ui"
assets = ["ui/assets"]
depends = ["contracts"]

[packages.server]
root = "server"
depends = ["contracts"]

[packages.desktop]
root = "desktop"
depends = ["contracts"]

[contracts]
manifest = "contracts/contracts.toml"

[targets.web]
kind = "web"
package = "ui"
entry = "Stock.Ui.Main"
styles = ["ui/styles/base.css"]
routing = "hash"
capabilities = ["ui", "http_client", "bridge"]

[targets.web.origins]
connect = ["https://api.stock.example"]

[targets.server]
kind = "server"
package = "server"
entry = "Stock.Server.Main"
capabilities = ["http_server", "sql", "app"]
providers = ["tls", "postgresql", "sqlite"]

[targets.server.http]
listen = "127.0.0.1:8080"

[targets.server.sql.main]
provider = "postgresql"
migrations = "server/migrations"

[targets.server.limits]
S-1 = 16

[targets.desktop]
kind = "desktop"
package = "desktop"
ui = "web"
entry = "Stock.Desktop.Main"
capabilities = ["desktop", "http_client"]

[targets.desktop.bridge]
operations = ["parts.export"]

[targets.desktop.packaging]
icon = "desktop/icon.png"
formats = ["deb", "tar.gz"]
```

| Rule | Status |
| --- | --- |
| The manifest uses the grammar of [TOML 1.0](https://toml.io/en/v1.0.0) for bare, basic double-quoted and dotted keys, tables, arrays of tables, basic double-quoted strings, signed 64-bit decimal integers, booleans, homogeneous arrays of strings or integers, and comments. Files use UTF-8 with LF or CRLF line endings. Floats, dates, inline tables, literal and multi-line strings, and other array element types are `E1020`. The reader lives in `src/project/toml.cpp`. | syntax reader implemented |
| An unknown key or a key of the wrong type is `E1020` at the key. A missing required key is `E1020` at its containing table unless a more specific rule below applies. | manifest model implemented |
| `schema` is 1. Another value is `E1021`. `project.language` is the language version the sources are written for, `"0.3"`. A compiler that does not implement it reports `E1021`. | manifest model implemented |
| `project.name` is lowercase letters, digits and hyphens. `project.id` is a lowercase reverse-domain identifier and names the desktop application, its origin and its data directory. `project.version` has three decimal components with no redundant leading zeroes. | manifest model implemented |
| A package is a source root with `root`, optional `assets` roots and optional `depends`. The dependency names refer to project packages or declared external dependencies. Local project paths are relative to the manifest, use `/` and stay inside the project directory (`E1025`). Internal package dependencies form no cycle. | manifest model and declared path resolution implemented |
| A module belongs to the package whose root contains its file. An import of a module in a package that is not the importer's own, not `Std`, and not in its `depends` closure is `E1030`. This is how a server module is kept out of a browser bundle: the `ui` package does not depend on `server`. | implemented in `neura build --target` and `neura check` ([Projects](../apps/Projects.md#packages-and-entries)): the check follows imports from the entry, or the checked files, through the project's packages before the compiler runs |
| An external package is `[dependencies.name]` with `path` (a relative path to a directory outside the project) or `archive` (a relative path to a file inside it), and `digest`. Its name is distinct from project package names. There is no registry and no network fetch in 0.3.0. An unpinned or unresolved dependency reports `E1032`. | manifest declaration, location resolution and hashing implemented |
| A target has `kind` (`web`, `server` or `desktop`), `package`, `entry`, `capabilities`, and by kind the tables below. A `desktop` target has `ui`, the name of a `web` target of the same project, whose built bundle it embeds unchanged (`E1023` otherwise). A shared-package project may omit `[targets]` or leave it empty. | manifest model implemented |
| The entry module exports the function its kind requires: `start: func(web.Context) -> Result<Unit>`, `serve: func(app.Context) -> Result<Unit>`, or `start: func(desktop.Context) -> Result<Unit>` (`E1024`). | partial: the entry is a program module (`main`) of the target's package, and an entry with no file is `E1024`; the entry functions are not checked |
| `capabilities` lists the capability groups the target's image may use, from those its kind allows ([overview](Apps030.md#capability-groups)). The console, clock and entropy groups are always available. A manifest cannot list them. An unknown or disallowed entry in the list reports `E1020`. A host service of a group that is not listed, or that the kind does not allow, reachable from the entry, is `E1027` at build. At run time the host grants only the listed groups in addition to the groups that are always available. | declaration check implemented; `E1027` checks the image's host imports at the `entry` span (a superset of what is reachable); the server host is given `--allow-fs`, `--allow-net` and `--allow-env` from the list |
| `providers` lists the provider components a server or desktop target links; a name the installed target component does not have, or a version outside what it supports, is `E1031`. | implemented for names: `sqlite` always, `postgresql` and `tls` in builds with them |
| `[targets.name.limits]` sets limits by their identifier ([overview](Apps030.md#finite-limits)). A single bound takes an integer. S-10, S-12 and S-13 take an array of exactly two integers in the order listed in the overview. U and W limits belong to web targets, S to server, D to desktop, and C to all three. A desktop bundle inherits the embedded web bundle's U and W limits. A value above its maximum, below 1, or for a limit of another target kind reports `E1028`. | manifest model implemented, enforcement proposed |
| `[targets.name.origins]` lists absolute origins by use. `connect` governs fetch, `load` images and media, `style` remote stylesheets, and `external` links a desktop application opens in the system browser. Each has a scheme, a DNS host or IP literal, and an optional decimal port. An IPv6 literal uses brackets. A path or a wildcard reports `E1020`. Anything outside the lists is unreachable. | shape check implemented, host enforcement proposed |
| A manifest contains no command, script or hook. No tool runs anything a manifest names, except the signing tools of `neura package --sign`. | implemented: the manifest model has no such key, and `--sign` is refused in 0.3.0 |

| Kind | Further keys |
| --- | --- |
| `web` | `styles` (global stylesheets, in order), `routing` (`"hash"` or `"history"`), `origins`, `limits` |
| `server` | `http` (`listen`, `tls_listen`, `admin_listen`, `trusted_proxies`), `sql.<pool>` (`provider`, `migrations`, `migrate`), `config` (`prefix`, `expose`), `providers`, `limits` |
| `desktop` | `ui`, `bridge.operations`, `origins`, `packaging` (`icon`, `formats`, `sign_identity`), `sql.<pool>`, `providers`, `limits` |

## The lock file

`neura.lock` is written by the tools and committed. It is the same TOML subset.

```toml
schema = 1
language = "0.3"
contracts_digest = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

[dependencies]
"charts" = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"

[components."server"."postgresql"]
version = "18.7"
digest = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"

[components."web"."browser-runtime"]
version = "0.3.0"
digest = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
```

Every digest is a SHA-256 string of 64 lowercase hexadecimal digits. The keys in
`[dependencies]` match the external dependency aliases in the manifest. Their
digests match the manifest pins. A component table
`[components.<target>.<component>]` records the name, version and digest of each
component resolved for that target. Its version starts with an ASCII letter or digit
and can also contain dots, underscores, plus signs and hyphens. An explicit
`[components.<target>]` represents a target whose resolution uses no components.
`contracts_digest` is present exactly when the resolver supplies a digest for
`contracts.lock`.

The canonical writer emits the root keys in the order shown above, followed by
the dependencies and the component tables. It sorts decoded dependency,
target and component names by byte order, writes dynamic names as basic quoted
strings, and uses LF line endings with a final newline.

| Rule | Status |
| --- | --- |
| It records `schema = 1`, the language version, each external dependency with its digest, each target component a target uses (the browser runtime, the desktop engine, each provider) with its version and digest, and the digest of `contracts.lock`. It records no source state, paths, timestamps or machine names. | lock model implemented |
| `neura build`, `neura package` and `neura contracts --check` read it and change nothing. A missing lock, a missing or extra entry, or a mismatch with a manifest pin or installed component metadata reports `E1022`. | `neura build` and `neura package` implemented; component metadata is the identity digest of each component (decision 12) |
| `neura build --update-lock` and `neura new` write it. `neura dev` writes it only when it is missing. | `neura build --update-lock` and `neura new` implemented |
| A digest that differs from resolved dependency or `contracts.lock` content reports `E1026`, with the expected and found digests. A build never downloads anything. | file resolution and lock comparison implemented, command integration proposed |

The resolver anchors path resolution at the manifest's canonical parent directory. It
resolves declared source, asset and migration roots to existing directories,
and declared stylesheets, icons, contract manifests and vendored archives to
existing regular files. Each component of a declared project path must resolve
inside the project. A symlink alias to another location inside the project
uses its canonical destination. An external dependency directory must be
disjoint from the project directory, including its ancestors. Component
availability and generated `contracts.lock` discovery belong to the caller.
The resolver hashes the caller's project-relative generated lock path if the
file is present. Archive and generated lock pins are SHA-256 of their raw bytes.

An external directory pin is SHA-256 of the ASCII bytes
`neura-project-tree-v1` followed by LF and then its entries in increasing
root-relative UTF-8 byte order. Each entry is encoded as a kind byte (`D` for
directory or `F` for regular file), an unsigned 64-bit little-endian path-byte
count, and the slash-separated path bytes. A file then adds its unsigned
64-bit little-endian byte count and 64 ASCII lowercase hexadecimal digits
of its SHA-256. Empty directories get entries. The root name, permissions,
timestamps and inode identifiers are absent. A regular hard link contributes
a record for its logical path and file bytes. Symlinks, repeated directory
identities, non-regular files, and names lacking an unambiguous UTF-8 spelling
fail resolution. The entry sort makes the pin independent of traversal order.
Keep the input tree stable while resolving it.

## Bundles

A bundle is a directory with `bundle.json` at its root. It is what a target builds to, what a host runs, and what a package wraps.

```text
dist/web/                          dist/server/
    bundle.json                        bundle.json
    index.html                         app.<digest>.nvr
    nvm.<digest>.js                    registry.json
    nvm.<digest>.wasm                  migrations/main/0001_parts.sql
    app.<digest>.nvr                   config.defaults.toml
    styles/<digest>.css
    assets/<digest>.png
```

| Rule | Status |
| --- | --- |
| `bundle.json` is canonical JSON: `schema` (1), `kind`, `id`, `name`, `version`, `language`, `image` (path and digest), `files` (each with path, SHA-256, byte length, media type, and `initial` for what the first commit needs), `requires` (the version of every axis the bundle depends on, and each provider with its version), `capabilities`, `origins`, `limits`, `contracts` (id and version), and for a desktop bundle `ui` (the digest of the embedded web bundle's `bundle.json`). It holds no time and no absolute path. | implemented ([Projects](../apps/Projects.md#bundlejson)); `contracts` is always empty and a web bundle adds `routing` and `base` |
| The identity of a bundle is the SHA-256 of `bundle.json`. | implemented |
| Every file of the directory is listed, and every listed file exists with its digest. A host checks each file before using it. A missing, extra or changed file, an unknown `schema`, or a required version the host does not have is a refusal before any image runs, naming the entry. | implemented in `neura run` and the desktop host |
| Every file name except `bundle.json`, `index.html` and `404.html` contains its digest, so a static host can cache it forever. References between files are relative, so a bundle works under any path. | implemented for web bundles; a server bundle's `server.json` and migrations and a desktop bundle's `desktop.json`, `registry.json` and embedded `web/` keep fixed names (decision 13) |
| A bundle contains no `.nra`, no template or stylesheet source, and no source map or name table unless the build was asked for them (`--source-map`), in which case they are separate files the image does not need. | implemented without `--source-map`, which a project build refuses in 0.3.0 |
| A web bundle's `index.html` is generated: the stylesheet links in their fixed order, the element the entry mounts on, the script that loads `nvm.js`, a message for browsers outside the matrix, and a content-security policy as a `meta` element: `default-src 'none'`, `script-src 'self' 'wasm-unsafe-eval'`, `style-src 'self'` plus the `style` origins, `img-src 'self'` plus the `load` origins, `font-src 'self'`, `connect-src 'self'` plus the `connect` origins, `base-uri 'none'` and `form-action 'none'`. The same text is in `bundle.json` for hosts that can send it as a header. | implemented |
| The build verifies a web bundle before writing `bundle.json`: no file of a package outside the target's closure, no host service among the image's imports outside the groups a web target may have, no provider, no configuration value not marked `expose`, and no development endpoint (`E1029`). | partial: host imports outside the web groups are `E1027`, native code and callbacks are refused; the package closure is `E1030` before the compiler runs; configuration and development endpoint checks are not implemented |
| `nvm.js` instantiates `nvm.wasm` by streaming when the host sends `application/wasm`, and from bytes otherwise, so a host with a wrong media type still works. | implemented by the browser runtime |
| A file over `W-1` is `E1029`. | implemented |
| A packaged archive is a bundle directory in a container; a host extracts and validates it as a directory. An arbitrary directory, or one whose `bundle.json` fails validation, is never executed. | implemented for the server package, whose `run` verifies the bundle through `neura-run`; the desktop package's host verifies the embedded web bundle |

## Commands

Each command is a tool of its own, reachable as `neura <command>` and directly, with the same arguments, diagnostics, streams and exit status ([Tooling](../lang/Tooling.md#exit-status)). Their entry points are `src/app/new.cpp`, `contracts.cpp`, `dev.cpp` and `package.cpp`, over a library `neura_project` in `src/project/`; nothing is added to `src/cli/main.cpp` beyond registration. When the split-product milestone lands, each entry file is the `main` of its sibling executable.

| Command | Direct form | Does |
| --- | --- | --- |
| `neura new <directory> --template <name>` | `neura-new` | Writes a project from a template built into the SDK: `fullstack`, `web`, `server` or `desktop`. Refuses a directory that is not empty. No network. The output is a function of the template, the name and the SDK version. |
| `neura contracts [--project <manifest>] [--check]` | `neura-contracts` | Generates or checks contract code ([Components](Components030.md#contracts)). Runs no project code. |
| `neura build --project <manifest> --target <name> [--out <directory>]` | `neurac` | Builds the target's bundle into `dist/<name>`. `--source-map`, `--no-optimize`, `--no-lift-hints` and `--update-lock` apply. A desktop target builds its web target first. Without `--project`, `--target` uses `neura.toml` of the current directory; without both, the command is today's single-file build. |
| `neura dev --project <manifest> --target <name> [--port <n>]` | `neura-dev` | Builds, runs and rebuilds on change, in the foreground. |
| `neura package --project <manifest> --target <name> [--format <f>] [--sign] [--report]` | `neura-package` | Wraps a built bundle with its host for delivery ([Desktop](Desktop030.md#packages)); for a server target, a `.tar.gz` holding the bundle, `neura-server-host` and the providers. |
| `neura run <bundle directory> [-- arguments]` | `neura-run` | Validates the bundle, then runs a server bundle with `neura-server-host` or a desktop bundle with the desktop host. A web bundle is not runnable: the message names `neura dev` and a static host. `.nvr` files and native containers run as today. |

| Rule | Status |
| --- | --- |
| Every command is direct and in the foreground. None contacts, starts or needs `neurad`, whether or not one is running; `neura run --runtime=auto` and `required` apply to a bundle as to an image. | implemented, except that `neura run` of a bundle takes no `--runtime` option |
| Builds are reproducible: the same sources, manifest, lock and components give the same bundle bytes on any supported host. | checked on this machine from two directories (`project-build`); not yet on a hosted runner |
| `neura dev` owns what it starts: a watcher that compares digests every 250 ms, a static HTTP server on `127.0.0.1` for a web target (port 4310 unless `--port`; a busy port is an error, `--port 0` asks the system for one), the server host for a server target, the desktop host for a desktop target. On exit, on end of input and on an interrupt it stops and reaps them all. It registers nothing with the system. | implemented ([Developing](../apps/Projects.md#developing)): the watcher hashes the declared inputs; each host runs in its own process group, which is ended and reaped |
| On a change, `neura dev` rebuilds and then reloads the page, or restarts the host. A change to stylesheet files alone replaces the stylesheet links in place, in their fixed order. State of the running program is not kept; replacing live Neura state is outside 0.3.0. | implemented |
| A build made by `neura dev` is marked `dev` in `bundle.json` and carries the reload endpoint. `neura package` refuses a `dev` bundle (`E1029`). | `neura package` and `neura run` refuse a `dev` bundle |
| `neura check`, the editor and `neura docs` understand a project: a file inside a project directory is checked with its package's roots and its target's rules, without `--project`. | `neura check` implemented: without `-I`, a file in a package of the nearest `neura.toml` above it gets its package's import roots and the `E1030` check; the editor, `neura docs` and the target's capability rules are not project-aware yet |

## Lowering and artifacts

No NeuraIR, NVR or tier change. A bundle wraps ordinary images; its schema and the manifest's are independent version axes ([overview](Apps030.md#version-axes)). A bundle is not a native container, and `neura build --native` is unchanged.

## Diagnostics

Spans are positions in `neura.toml`, `neura.lock` or `contracts.toml` unless noted.

| Code | Title | Span | Fix |
| --- | --- | --- | --- |
| `E1020` | malformed project manifest | The token, key or value | The form expected |
| `E1021` | unsupported manifest schema or language version | The value | The versions this SDK supports |
| `E1022` | lock file missing or out of date | The lock entry, or the manifest key without one | `neura build --update-lock` |
| `E1023` | invalid target | The target's key: an unknown kind, a missing package, a `ui` that is not a web target | The valid values |
| `E1024` | entry module without the required entry | The `entry` value, with a label at the module if it exists | The function to export |
| `E1025` | path outside the project | The path | A path inside the project |
| `E1026` | content does not match its digest | The digest, with both values | Restore the content, or update the lock deliberately |
| `E1027` | capability used but not declared | The source expression that reaches the service, with the service's group | Add the group to `capabilities`, or remove the use |
| `E1028` | limit out of range | The value, with the range | A value in range |
| `E1029` | bundle cannot be written | The file or import at fault: too large, a forbidden import or value in a web bundle, a `dev` bundle given to `package` | By case |
| `E1030` | module not available to this package or target | The import in source | Add the dependency, or move the code to a shared package |
| `E1031` | unknown or incompatible provider | The provider name | The providers installed |
| `E1032` | dependency not pinned or not found | The dependency table | A `path` or `archive` with a `digest` |
| `E1033` | package cannot be built here | The format, or `--sign` | The system or input needed |

## Edge cases

Accepted:

1. A manifest with one web target and one package.
2. Comments, blank lines and any order of tables.
3. A package with two asset roots.
4. Two web targets with different entries in one project.
5. A desktop target whose `ui` target is built in the same command.
6. `neura build --target web` run from a subdirectory with `--project ../neura.toml`.
7. A project path with spaces and non-ASCII characters.
8. `--out` naming an empty directory outside the project.
9. Building twice: identical bytes; building on another machine: identical bytes.
10. A build with `--source-map`: the map is a separate listed file and the image digest is unchanged.
11. A web bundle served from a subpath and from a host that sends `application/octet-stream` for `.wasm`.
12. `routing = "history"`: `404.html` is written and the build says the host must serve it.
13. `neura run dist/server -- --config prod.toml`; a web target that lists `bridge` and whose bundle runs unchanged in a browser, where the bridge is absent.
14. `neura dev` stopped by an interrupt, by end of input, and by closing its terminal: no child remains in each case.
15. A stylesheet edited under `neura dev`: the link is swapped, the page is not reloaded, the order is unchanged.
16. A single-file `neura build app.nra -o app.nvr` in a directory that also holds `neura.toml`: built as a single file.
17. `neura new stock --template fullstack` followed at once by a build of every target.
18. Every command through `neura` and through its direct tool, with a service absent and with one running.

Rejected:

19. `name = 'stock'` (a literal string), `ratio = 1.5`, an inline table: `E1020`.
20. An unknown key `[targets.web] entri = ...`: `E1020`, suggesting `entry`.
21. A repeated table or key: `E1020`.
22. `schema = 2`; `language = "0.4"`: `E1021`.
23. No `neura.lock`; a lock that lists a provider the manifest dropped: `E1022`.
24. `kind = "mobile"`; a target without `package`; a desktop target with `ui = "server"`: `E1023`.
25. An entry module that exists and exports no `start`; one whose `start` takes the wrong context: `E1024`.
26. `root = "../shared"`; `assets = ["/usr/share/icons"]`; a symbolic link that leaves the project: `E1025`.
27. A vendored archive changed after locking: `E1026`.
28. A web target whose code reaches `neura.fs.open`; a server target using SQL without `sql` listed: `E1027`.
29. `S-1 = 0`; `S-1 = 5000`; `S-1` under a web target: `E1028`.
30. A `connect` origin with a path or a `*`: `E1020`.
31. The `ui` package importing `Stock.Server.Repository`: `E1030` at the import.
32. A package dependency cycle: `E1020`, listing it.
33. A web target whose image imports `Std.DynamicForeign`: `E1027`.
34. A configuration key not marked `expose` read by web code: `E1029`.
35. An asset of 2 GiB: `E1029`.
36. `providers = ["mysql"]`: `E1031`.
37. `[dependencies.charts]` with a `path` and no `digest`: `E1032`.
38. `neura package` of a bundle built by `neura dev`: `E1029`.
39. `neura package --format msix` on Linux: `E1033`.
40. `neura new` into a directory that holds a file: refused, nothing written.
41. `neura run dist/web`: refused, with the two ways to serve it.
42. `neura run` of a directory without `bundle.json`, and of one with an extra file: refused before anything runs.
43. A bundle with one byte of an asset changed: refused, naming the file.
44. A bundle that requires provider interface 2 on a host with 1: refused, naming the axis and both versions.
45. `neura dev --port 80` without the right to bind it, or with the port busy: an error, no child left.
46. A manifest key `postbuild = "..."`: `E1020`; no manifest can name a command.

## The reference application

One project, built by `neura new --template fullstack` and kept in `examples/apps/stock/`, is the fixture every milestone's acceptance and every budget uses.

Status (2026-11-18): `examples/apps/stock` is the `fullstack` template of `neura new` as far as the libraries allow: `Contracts.Parts` (starting parts and the name rule), a page with a counter over the contract, a server answering `GET /parts` and `POST /parts?name=` with the shared rule, and a desktop child with one window and `parts.export` as CSV. The records, `PartsApi`, generated adapter, SQL repository, migrations, sessions and the journeys below are not implemented.

| Part | Content |
| --- | --- |
| Contracts | `Part` (an `int64` id, a name, an `int32` count, a position), a page record holding a `FrozenList<Part>`, replies with a `Rejected` variant, the interface `PartsApi` with `list`, `add`, `rename`, `restock`, `move` and `export`, shared validation of names |
| Web | A keyed list, an add form with a controlled input and validation, inline rename, reorder, loading, empty and failed states, two routes, a command per operation, a CSS module and a global stylesheet with an asset |
| Server | Routes from the generated adapter, a service, a repository over `Std.Data.Sql` that runs on PostgreSQL and on SQLite, three migrations, session authentication |
| Desktop | The web bundle, one window, a menu, and `export`: the parts as CSV (`Std.Encoding.Csv`) written through a save dialog's `GrantedFile` |

The journeys: add, rename, restock and reorder parts; reload and relaunch and find them; a rejected name shown beside its field; a slow list cancelled by leaving the route; an export saved, cancelled and denied; a request that traps while others complete; the same with sources, names and maps stripped; one byte of an asset, one provider version and one operation schema changed, each refused.

## Order of work

The integration milestone (A6). Each step is one commit with its targeted tests.

| Step | Content | Tests |
| --- | --- | --- |
| 1 | This note | Document checks |
| 2 | `src/project/`: the TOML subset, the manifest and lock models, `E1020` to `E1028`, `E1032` | A fixture per rejected case 19 to 30, 32, 37; round trips |
| 3 | Package closures and `E1030`; capability checking `E1027`; project awareness in `check` and the editor | Cases 28, 31, 33 |
| 4 | Bundles: writer, validator, `neura build --target`, `neura run` of a bundle, `E1029`, `E1031` | Cases 5 to 13, 34 to 36, 41 to 44; two builds compared |
| 5 | `neura new` and its templates | Cases 17, 40; the template's own tests |
| 6 | `neura dev` for the three kinds | Cases 14, 15, 45; process-tree checks |
| 7 | `neura package` for server and desktop | With [Desktop](Desktop030.md#order-of-work) steps 7 and 8 |
| 8 | The reference application and its journeys in CI: browsers, both databases, desktop under Xvfb | The journeys above |
| 9 | Every budget measured and recorded; the tools in the tool registry, install components and `packaging/qualify.sh` | Case 18; the budget scripts |

## Gate and acceptance

- One project builds `web`, `server` and `desktop`; the desktop package embeds the web bundle with the same digest.
- Every command works through `neura` and directly, with identical output and status, with no service present and with one running, and starts no service.
- Two builds of each target are byte-identical, on this machine and on a hosted runner.
- Each rejected case has a fixture with its code and span; each refused bundle is refused before an image runs.
- A web bundle contains no file, import, value or endpoint the verifier forbids, checked by a second, independent scan of the output in CI.
- `neura dev` leaves no process, socket or registration behind, however it is stopped.
- The journeys pass from packaged artifacts, with sources and maps stripped, on every row of the matrices that a runner or this machine can reach; the rest are listed as unqualified.
- The minimal SDK and the runtime-only installation contain no browser engine, database driver or TLS library; those are in target components, measured apart (budgets B-W1 to B-D3). *Superseded by the owner on 2026-11-18:* the server host, the SQL providers and TLS stay in the core component; the browser engine stays outside the SDK.

## Migration

No 0.2 workflow changes: a directory without `neura.toml` behaves as before. A multi-file program that passed `-I` roots can keep doing so, or declare them as packages. A deployment that copied a single `.nvr` can keep doing so for programs that are not applications; an application is deployed as a bundle or a package.

## Approved design decisions

1. The manifest and the lock are a strict TOML subset read by new code; unknown keys are errors.
2. Packages are source roots with declared dependencies, and target separation is the import rule `E1030`, not a tag on modules.
3. There is no package registry: external packages are local directories or vendored archives with digests.
4. The lock pins dependencies and target components, not the project's own sources, and only `--update-lock` and `neura new` write it.
5. A bundle is a directory identified by the digest of a canonical `bundle.json`; every file is listed and checked.
6. A web bundle is not runnable by `neura run`; a server bundle and a desktop bundle are.
7. `neura dev` polls digests rather than using a system watcher, binds loopback only, and fails on a busy port.
8. Limits are set by their identifiers under `[targets.name.limits]`.
9. A manifest can name no command.
10. The reference application is a stock list in `examples/apps/stock/`, with CSV export as its native operation.
11. A desktop target's `package` key selects its source package. Desktop deployment settings use `[targets.name.packaging]`. TOML gives each key a single definition, so using the same key for the source string and the settings table is invalid.

## Decisions taken without the owner

Taken by the A6 lane on 2026-11-18, awaiting owner confirmation:

12. A target component's lock digest is the SHA-256 of `neura-component-v1`, its target kind, name and version, each followed by LF. Target components carry no installed metadata in 0.3.0; the bytes of the browser runtime are pinned by each bundle's file table instead.
13. Files a host finds by name keep their names: a server bundle's `server.json` (the manifest's host settings) and `migrations/<pool>/NNNN_name.sql` (the host orders migrations by number), and a desktop bundle's `desktop.json`, `registry.json` and embedded `web/` directory, which together are the package directory the desktop host loads. A desktop bundle's `image` is its native child, `bin/app-<sha256>`.
14. The import root of a package is the parent of its root, so modules are named from the root's own name (`ui/main.nra` is `Ui.Main`) and a project needs no extra directory level. The entry is a program module with `main` until the entry functions of the table above are implemented.
15. `bundle.json` names host interfaces `browser_adapter`, `ui_host`, `server_host`, `desktop_host` and `provider.<name>`, all at version 1.
16. A server package is a `.tar.gz` of `<name>-<version>/` holding `bundle/`, `bin/neura-run`, `bin/neura-server-host`, `lib/neura/libneura-core-<version>` (which holds the providers) and a `run` script that serves the bundle through `neura-run run`, so the bundle is verified at every start. The gzip stream uses stored deflate blocks: it is reproducible without a compression library, and larger than a compressed one.
17. A desktop package is built by the desktop packager of A5 (`scripts/package-desktop.py`, installed in `share/neura/scripts`) from the desktop bundle, which is the package directory the host loads; the target's `packaging.icon` and the engine named by `NEURA_DESKTOP_ENGINE` are its inputs.
18. `neura dev` writes its bundles to `dist/dev/<target>`, so a development build never replaces a release build in `dist/<target>`. A desktop target's embedded web bundle is not marked `dev` (the desktop host refuses one); the host is restarted after a build instead. The reload endpoint is `/__neura/dev` on the development server, polled by the bundle's `neura-dev.js` every 250 ms.
