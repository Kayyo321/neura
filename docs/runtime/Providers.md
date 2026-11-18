# Host providers of the application hosts

This page owns the host services and protocols of the application hosts that [the application platform](../design/Apps030.md#owning-pages) adds in 0.3.0. It starts with the browser: the interface between `nvm.wasm` and its adapter `hosts/web/nvm.js`, the services a browser binds, how a browser run waits, and the byte encodings of the UI services. The services every host shares, and the overlap and cancellation rules they follow, stay in [the NVM page](NVM.md#host-services) and [Concurrency](../lang/Concurrency.md#104-host-io).

*Status: implemented in the browser runtime on 2026-11-17 by autonomous agents, awaiting owner confirmation.* The model host of `neura run --ui-host=model`, the desktop and server hosts, and `neura.http.fetch` are not implemented yet; their sections are added with them.

## Browser adapter interface

`nvm.wasm` exports `neura_wasm_interface()`, which returns the interface version, 1. The adapter checks it before any other call and rejects a module with another version, naming both. Version 1 consists of the exports below and the records they use. Every number is little-endian and every pointer is a wasm32 address.

| Export | Arguments | Result |
| --- | --- | --- |
| `neura_wasm_interface` | none | `uint32` version |
| `neura_wasm_open` | image pointer and size, argument table and count, grant text pointer and size, tier 0 to 2 (plus `0x100` to form tier 2 regions instead of function programs, for measurements only), step limit (`uint64`, 0 for the default) | A run handle, or 0 with the reason in `neura_wasm_error` |
| `neura_wasm_advance` | run, step budget 1 to 4,096 | A pointer to the 88-byte report below |
| `neura_wasm_respond` | run, completion pointer and size | Success; answers the request the last report named |
| `neura_wasm_deliver` | run, sequence (`uint64`), completion pointer and size | Success; valid only after a `poll` or `waiting` report |
| `neura_wasm_clock` | run, task clock reading in nanoseconds (`int64`) | The number of timers and deadlines that expired, or -1 |
| `neura_wasm_has_proof`, `neura_wasm_prove_idle` | run | Whether a proof is queued; the milliseconds one idle proof took, -1 when none ran, -2 on failure |
| `neura_wasm_last_proof` | run | The function and block of the last idle proof as `function * 2^32 + block`, for measurements |
| `neura_wasm_tier_stats` | run | The lifting statistics of the run's cache as `neura run --tier-stats` prints them, NUL-terminated, valid until the next call; for measurements. Its step line reads zero: the browser does not collect step counters |
| `neura_wasm_close` | run | none |
| `neura_wasm_error` | none | The last failure as NUL-terminated UTF-8 |

The argument table holds one `{ uint32 pointer, uint32 size }` record per argument. The grant text lists the services the embedder serves, one `name/version` per line, from the [browser services](#browser-services). Granting a service the runtime does not know fails the open.

The report:

| Offset | Field | Meaning |
| ---: | --- | --- |
| 0, 8, 16 | `uint64` steps, peak heap cells, peak stack cells | Counters of the run so far |
| 24 | `uint64` sequence | `host`: the request; `cancel`: the request to cancel |
| 32 | `int64` deadline | `poll` and `waiting` with a timer: when the earliest timer is due |
| 40 | `uint32` signal | 0 `yield`, 1 `completed`, 2 `trapped`, 3 `host`, 4 `poll`, 5 `waiting`, 6 `cancel` |
| 44 | `uint32` flags | 1: the request overlaps; 2: it can be cancelled; 4: a timer is armed |
| 48 | `{ pointer, size }` | `trapped`: the diagnostic; `host`: the encoded arguments |
| 56, 60, 64, 68 | `int32` status, `uint32` reason, function, block | `completed` and `trapped` |
| 72 | `{ pointer, size }` | `host`: the service name |
| 80 | `uint32` version | `host`: the service's interface version |

The pointers in a report stay valid until the next call into the run. The adapter copies what it needs at once, because a call that grows memory detaches earlier views of it.

### Value wire

A host value crosses as its 64 bits, a `uint32` byte count and the bytes, then a `uint32` element count and the elements, recursively. Scalars use the bits in their frame-slot encoding: an `int32` is its low 32 bits. Text and `uint8` lists use the bytes; tuples and other lists use the elements. A request's arguments are their values in parameter order. A completion is one byte, 1 for success and 0 for an error, followed by one value of the success or error type. The runtime checks a completion against the import's types; an ill-typed completion traps the run, as in every host. A truncated completion, trailing bytes, or nesting deeper than 64 is a failure of the call that delivered it.

## Browser services

All have a `uint32` error. The adapter key is the `host` member of `new NvmExecution(module, image, { host })` that grants the service.

| Service | Version | Arguments | Result | Overlaps | Key |
| --- | --- | --- | --- | --- | --- |
| `neura.console.write_line` | 1 | text | none | no | `console` |
| `neura.console.write` | 1 | `int32` stream (1 stdout, 2 stderr), `uint8[list]` bytes | `int32` count | no | `consoleWrite` |
| `neura.clock.wall` | 1 | none | `[int64 seconds, int32 nanos]` | no | `clockWall` |
| `neura.clock.monotonic` | 1 | none | `int64` nanoseconds | no | `clockMonotonic` |
| `neura.clock.sleep` | 1, 2 | `int64` nanoseconds | none | yes; version 2 can be cancelled | `sleep` |
| `neura.entropy.fill` | 1 | `int32` count, at most 1 MiB | `uint8[list]` | no | `entropy` |
| `neura.ui.root.open` | 1 | selector text | `int64` root | no | `ui` |
| `neura.ui.root.close` | 1 | `int64` root | none | no | `ui` |
| `neura.ui.template` | 1 | `int64` root, descriptor text | `int64` template | no | `ui` |
| `neura.ui.commit` | 1 | `int64` root, `uint8[list]` operations | none | no | `ui` |
| `neura.ui.event.next` | 1 | `int64` root, `int32` maximum | `uint8[list]` events | yes, can be cancelled | `ui` |
| `neura.ui.stats` | 1 | `int64` root | `[int64 nodes, int64 listeners, int64 templates, int64 queued]` | no | `ui` |
| `neura.ui.focus` | 1 | `int64` root, element `id` text | none | no | `ui` |
| `neura.history.current` | 1 | none | path text | no | `history` |
| `neura.history.push`, `replace` | 1 | path text | the current path | no | `history` |
| `neura.history.next` | 1 | none | path text | yes, can be cancelled | `history` |
| `neura.history.link` | 1 | path text | the `href` text | no | `history` |

The runtime answers two kinds of service itself, without reporting a request to the adapter:

- `neura.process.exit` version 1 ends the run with its status, as on every host.
- A service of the table that the embedder does not grant, and a standard service of a group a browser never grants (`neura.console.read`, `neura.env.get`, `neura.fs.*`, `neura.net.*`, `neura.ffi.*`, `neura.http.*`, `neura.bridge.call`, `neura.sql.*`, `neura.app.*`, `neura.desktop.*`), binds and answers error 14. An image that reaches one runs and fails locally, as [Apps030](../design/Apps030.md#capability-groups) requires. A service of the second kind binds with the image's own parameter and result types; its error type must be `uint32`, because error 14 is the only value that crosses. An overlapping request among them is answered at the next scheduling point, like any other.

Any other import fails the open with the import's name and version. `src/wasm/entry.cpp` holds the table; `hosts/web/nvm.js` holds the argument and result conversions.

`browserHost({ output, ui, history })` in `nvm.js` supplies the defaults of a page: the console writes each line to `console.log`, and `neura.console.write` keeps the bytes of each stream until their LF and writes whole lines to `console.log` (stdout) or `console.error` (stderr), the wall clock is `performance.timeOrigin + performance.now()`, the monotonic clock and the task clock are `performance.now()`, both in nanoseconds, a sleep is a timer, and entropy is `crypto.getRandomValues`. A sleep ends once the monotonic clock has reached its deadline, even when a timer fires early. A negative sleep, an entropy count outside 0 to 1 MiB, or a console stream other than 1 or 2 answers error 4. Console input, `neura.console.read`, is never granted.

## Scheduling

`run()` drives a run the way `drive()` drives a native one ([NVM](NVM.md#host-services)), and it never polls:

- A request that does not overlap is answered before the run advances. A service may answer with a promise; the run stays suspended at the request, the page keeps running, and the answer is given when the promise settles. This is how a program without tasks sleeps or waits for an event.
- An overlapping request is started and the run goes on. Its answer is queued when its promise settles, and delivered at the next `poll` or `waiting` report, in the order the answers arrived.
- `poll` delivers the queued answers and, when a timer is armed, gives the task clock's reading.
- `waiting` waits for the first of: an answer, or the clock reaching the deadline. The wait is one browser timer and one promise. A queued idle proof may run during it. A wait with neither a pending request nor a timer is a deadlock of the embedding and fails the run.
- `cancel` aborts the request's `AbortSignal` and answers error 15 at once unless its answer is already queued. A later answer from the service is ignored, so every request is answered exactly once.
- A service that throws, or whose promise rejects, closes the run: it may already have had an effect, so the request is never issued again.

The slice and proof rules of the [browser runtime](../design/Apps030.md#execution-in-the-browser-a2) are unchanged: at most 4,096 steps between checks of the 8 ms slice budget, and a queued proof only on an idle callback when no event is waiting.

### Idle proofs

A browser run proves blocks only on idle callbacks, one attempt at a time, and each attempt is bounded so that it stays a short task on the page's main thread (budget B-W7). The overflow analysis of one attempt gets at most 2^19 work units, about 8 ms of native work, and a search at most 8 ms of wall time. A block whose proof needs more work than one attempt allows stays on tier 0 for the rest of the run; a search cut short is tried again while the image's search budget of 250 ms lasts. These bounds change which blocks are lifted, never what a run does. Native hosts keep their unbounded attempts.

## UI services

`hosts/web/ui.mjs` implements the `ui` group over the [DOM adapter](../../hosts/web/README.md) and `hosts/web/ui-wire.mjs` holds the encodings. A root handle and a template handle are numbers from one counter, starting at 1 for each `UiHost` and never reused; a call with a handle the host did not issue, or one already closed, answers error 4.

| Service | Behavior | Errors |
| --- | --- | --- |
| `root.open` | Opens a root on the first element the selector matches | 1: no element; 3: the element is inside, or contains, another root; 4: an invalid selector; 100: `U-9` roots are open |
| `root.close` | Empties the element, detaches every listener, clears the queue, and answers a waiting `event.next` with an empty batch | 4 |
| `template` | Checks that the descriptor is JSON with `encoding` 1 and within `U-1` (4,096 nodes), `U-2` (depth 64) and `U-3` (1,024 slots), and returns a handle | 4 |
| `commit` | Decodes the batch, validates all of it, then applies it; marks `neura:first-commit` once and `neura:commit-end` after each | 4: undecodable or rejected batch, nothing changed; 100: the browser failed while applying, and the root is faulted |
| `event.next` | Answers at once when events are queued, or waits for the next; at most `maximum` (1 to 65,536) records | 3: another `event.next` is waiting on the root; 4; 15: cancelled |
| `stats` | Managed nodes (the container excluded), installed listeners, registered templates, queued events | 4 |
| `focus` | Moves the keyboard focus to the element whose `id` is the text, when the root's element contains it (the root's own element excluded); the browser fires the element's `focus` and `blur` events as for any focus change. Added in 0.3.0 for `ui.FocusHost` | 1: the root holds no such element; 4: an unknown root or an empty `id` |

A cancelled `event.next` whose answer the adapter had not yet delivered when the cancellation reached it (`cancelRequest` in `nvm.js` discards such an answer) puts the records it took back at the front of the queue, so a wait that loses a race loses no event; a cancelled `history.next` keeps its navigation for the next wait in the same way.

A faulted root delivers no events and refuses every commit with 100 until it is closed. There is no reset service: `Std.Ui` resets a root by closing it and opening a new one on the same element.

### Commit encoding

Version 1. A batch is the byte 1, a `uint32` operation count, then the operations. Text is a `uint32` byte count and UTF-8 bytes; invalid UTF-8 rejects the batch. Node 0 is the root's element, which is never an insertion anchor, so an anchor of 0 means "append". Each operation starts with its code and the `uint32` node it acts on. The operations and their order rules are those of the [DOM adapter](../../hosts/web/README.md#decoded-operations).

| Code | Operation | Fields after the node |
| ---: | --- | --- |
| 1 | `remove` | none |
| 2 | `element` | `uint32` parent, `uint32` anchor, `uint8` SVG flag, name text |
| 3 | `text` | `uint32` parent, `uint32` anchor, value text |
| 4 | `move` | `uint32` parent, `uint32` anchor |
| 5 | `setText` | value text |
| 6 | `attribute` | name text, `uint8` present, the value text when present |
| 7 | `variable` | name text, `uint8` present, `float64` number and unit text when present |
| 8 | `handler` | event text, `uint64` handler (0 detaches), `uint8` flags: 1 `prevent_default`, 2 `stop_propagation` |
| 9 | `property` | name text, `uint8` kind (0 text, 1 Boolean), then the text, or one byte 0 or 1 |

A flag byte other than 0 or 1, an unknown code or kind, a count larger than the bytes can hold, and trailing bytes reject the batch.

`Std.Web`'s host produces these batches with `Std.Ui`'s reconciler. A commit of a child component that rendered without its parent is an ordinary version 1 batch that holds only the operations of the child's nodes ([subtree commits](../design/Components030.md#subtree-commits)); the encoding has no component boundary.

### Event encoding

Version 1. A batch is the byte 1, a `uint32` record count, then the records. Each record is its kind, the `uint32` node and the `uint64` handler of the listener, then its fields. Modifiers are one byte: 1 alt, 2 ctrl, 4 meta, 8 shift. A button is the DOM's button number as `uint8`; -1 (no button changed, as in `pointermove`) is 255. The pointer kind is 0 mouse, 1 pen, 2 touch, 3 other.

| Kind | Event | Fields |
| ---: | --- | --- |
| 1, 2 | `click`, `dblclick` | button, `float64` x, `float64` y (relative to the element), modifiers, `int32` detail |
| 3 | `input` | value text, `uint8` checked, `uint8` composing |
| 4 | `change` | value text, `uint8` checked |
| 5 | `submit` | submitter text (the submitter's `id`, or empty) |
| 6, 7 | `keydown`, `keyup` | key text, code text, modifiers, `uint8` repeat |
| 8, 9 | `focus`, `blur` | none |
| 10, 11, 12 | `pointerdown`, `pointerup`, `pointermove` | `float64` x, `float64` y, button, pointer kind, modifiers |
| 13 | `scroll` | `float64` top, `float64` left |
| 14 | `toggle` | `uint8` open |
| 15 | `close` | return value text |
| 16 | input overflow | `uint32` dropped; node 0, handler 0 |

### Event queue

Each root queues the records of its listeners in arrival order. A `pointermove` or `scroll` replaces the queued record of the same node and event in place. When `U-7` (1,024) records are queued, the root is overloaded: a record that cannot be coalesced is dropped and counted, the latest dropped `input` record of each element is kept, and the element carries `data-neura-overloaded`. Once there is room again (an event arrives, or `event.next` takes records), the queue gains an input-overflow record with the dropped count, then the kept `input` records, and the attribute is removed. The overflow record follows every record queued before the first drop.

### Vectors

Generated with `encodeCommit` and `encodeEvents` of `hosts/web/ui-wire.mjs` (`node --input-type=module` in `hosts/web`); `tests/apps/web/check.py` round-trips the operation corpus and every operation and event kind through the encoders and the decoders.

| Value | Bytes (hex) |
| --- | --- |
| A commit: `element` 1 `p` in the root, `text` 2 `Hi` in it, then `handler` on 1 for `click` with handler 7 and `prevent_default` | `01030000000201000000000000000000000000010000007003020000000100000000000000020000004869080100000005000000636c69636b070000000000000001` |
| An event batch: one `click` on node 1, handler 7, button 0, x 2.5, y 4, ctrl and shift, detail 1 | `01010000000101000000070000000000000000000000000000044000000000000010400a01000000` |
| An event batch: one input overflow of 3 | `01010000001000000000000000000000000003000000` |

## History services

`HistoryHost` in `ui.mjs` implements the `history` group. A path is absolute (it starts with `/`), at most 8 KiB, without control characters; another path answers error 4. With `mode: "hash"`, the default, the path is kept in the fragment (`#/parts/7`), so any static file host serves the page. With `mode: "history"` it is the URL's path and query below `base` (default `/`; `new HistoryHost({ mode: 'history', base: '/app/' })` keeps `/parts/7` at `/app/parts/7`), and the server must answer every application path below the base with `index.html` (the server rule of [web bundles](#web-bundles)). Paths are percent-encoded in the URL and decoded when read; a malformed escape is read as written.

`push` and `replace` change the URL without loading a page and answer the current path. `next` waits for a navigation the program did not make (back, forward, an edited fragment); navigations while nobody waits coalesce to the latest. A second waiting `next` answers 3, and a cancelled one 15. `link` (version 1, added in 0.3.0) answers the `href` of a link to the path in the page's mode: `#/parts/7` with hash routing, and the base followed by the path, `/parts/7` or `/app/parts/7`, with history routing. A program learns the routing mode only through `link`, so one image runs under either mode.

## Web bundles

`scripts/build-web-bundle.py --image app.nvr --id org.example.app --out dist/web` assembles the static directory of [Projects](../design/Projects030.md#bundles) from a built image and the browser runtime. It stands in for `neura build` of a `web` target until the project tools exist. `--source app.nra` runs `neura build` first (`--neura` names the executable). The stylesheets and assets that the build listed in `<image>.styles.json` ([Stylesheets](../apps/Css.md#build-output-and-order)) are copied at their paths, `styles/<sha256>.css` and `assets/<sha256>.<extension>`, after their digests and sizes are checked; the stylesheets are linked from `index.html` in the listed order, and only those are `initial`. [Your first Neura web app](../apps/WebApp.md) walks through it.

| File | Content |
| --- | --- |
| `index.html` | The content-security policy as a `meta` element, the mount element (`--mount`, default `app`), a JSON data block naming the image and `nvm.wasm` with their SHA-256, the boot module with its `integrity`, a message for browsers without WebAssembly exception handling, and a failure banner |
| `bundle.json` | Canonical JSON (sorted keys, no whitespace): `schema` 1, `kind` `web`, `id`, `name`, `version`, `image`, `files` (path, SHA-256, byte length, media type, `initial`), `requires` (`browser_adapter` 1, `ui_host` 1), `capabilities`, `origins`, `limits` (`W-1`), `contracts`, `csp`, `routing`, `base` (the URL path of a history-routed bundle) |
| `boot.<sha256>.mjs`, `nvm.<sha256>.js`, `ui.<sha256>.mjs`, `dom.<sha256>.mjs`, `ui-wire.<sha256>.mjs`, `nvm.<sha256>.mjs` | The browser host, with each import renamed to the file's digest name |
| `nvm.<sha256>.wasm`, `app.<sha256>.nvr` | The runtime and the image |
| `styles/<sha256>.css`, `assets/<sha256>.<extension>` | The image's stylesheets and assets, when it has any |

The policy is `default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self'; img-src 'self'; font-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'none'`, and the same text is the `csp` field. The boot module fetches the image and `nvm.wasm` with their `integrity`, so a changed file stops the start. It compiles `nvm.wasm` by streaming when the host sends `application/wasm` and from bytes otherwise, runs the image with `browserHost` and a `UiHost` and `HistoryHost`, and sets `data-neura-state` on the root element to `running`, `completed`, `failed` or `unsupported`. A trap shows the failure banner with the trap's diagnostic. The boot module fetches the image and the runtime relative to its own URL, which is the bundle's directory, and gives the `UiHost` that directory as `assetBase`, so bundled media resolve against the bundle whatever the page's path.

With `--routing history` (and `--base`, default `/`, the URL path the bundle is served under), the page's own references (the boot module and the stylesheets) start at the base instead of `./`, because the page is also served at deep application paths such as `/parts/7`, where a relative reference would resolve below `/parts/`; the policy's `base-uri 'none'` rules out a `<base>` element. The boot configuration and `bundle.json` carry `base`. The server rule: the host serves the bundle's files at `<base><file>` and answers every other path below the base with `index.html`. The script also writes `404.html`, a copy of `index.html`, for static hosts that serve that file for unknown paths (GitHub Pages, for example), and prints the rule. With hash routing the references stay relative and the bundle works under any path.

The script does not yet refuse an image whose imports fall outside the groups a web target may have, and it does not name module imports with `integrity`, which only the boot module carries.

## Not provided in the browser yet

- Recording and replaying a browser run. The services' requests and answers are explicit on the wire, so a trace can be written by the adapter; no trace format for the browser exists yet.
- `neura.http.fetch`, gesture actions, and a model host for `neura run`. The native runner binds the UI and history services and answers each with error 14, so an image that imports `Std.Web` loads natively and `web.mount` returns `Err`.
