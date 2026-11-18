# Host providers of the application hosts

This page owns the host services and protocols of the application hosts that [the application platform](../design/Apps030.md#owning-pages) adds in 0.3.0. It starts with the browser: the interface between `nvm.wasm` and its adapter `hosts/web/nvm.js`, the services a browser binds, how a browser run waits, and the byte encodings of the UI services. [Desktop host services](#desktop-host-services) follow: the channel between a desktop window host and its native child, and the services it carries. The services every host shares, and the overlap and cancellation rules they follow, stay in [the NVM page](NVM.md#host-services) and [Concurrency](../lang/Concurrency.md#104-host-io).

*Status: implemented in the browser runtime on 2026-11-17 by autonomous agents, awaiting owner confirmation; the desktop section has its own status line.* The model host of `neura run --ui-host=model` is not implemented yet; the server host's services are in [Server host](ServerHost.md). `neura.http.fetch` is served in the browser only.

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
| `neura.http.fetch` | 1 | method text, URL text, headers (text list), body (`uint8[list]`), `int64` timeout in nanoseconds, `uint32` credentials | `[uint32 status, text list headers, uint8[list] body]` | yes, can be cancelled | `http` |

The runtime answers two kinds of service itself, without reporting a request to the adapter:

- `neura.process.exit` version 1 ends the run with its status, as on every host.
- A service of the table that the embedder does not grant, and a standard service of a group a browser never grants (`neura.console.read`, `neura.env.get`, `neura.fs.*`, `neura.net.*`, `neura.ffi.*`, `neura.http.server.*`, `neura.bridge.call`, `neura.sql.*`, `neura.app.*`, `neura.desktop.*`), binds and answers error 14. An image that reaches one runs and fails locally, as [Apps030](../design/Apps030.md#capability-groups) requires. A service of the second kind binds with the image's own parameter and result types; its error type must be `uint32`, because error 14 is the only value that crosses. An overlapping request among them is answered at the next scheduling point, like any other.

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

A browser run proves blocks only on idle callbacks, one attempt at a time, and each attempt is bounded so that it stays a short task on the page's main thread (budget B-W7). The overflow analysis of one attempt gets at most 2^19 work units, about 8 ms of native work, and a search at most 8 ms of wall time. An attempt that runs out keeps what the analysis settled, and the block is queued again at its next visit; the next attempt resumes there, so a graph that needs more work than one attempt allows is proven over several idle callbacks. A block stays on tier 0 for the rest of the run only when an attempt settles nothing more than the one before it. A search cut short is tried again while the image's search budget of 250 ms lasts. These bounds change which blocks are lifted, never what a run does. Native hosts keep their unbounded attempts.

A function program formed while some of its blocks wait for a proof runs those blocks by reference. A native host forms it again at four times its arrivals, at most four times. A browser forms it again once an idle proof has settled one of the function's blocks since it was formed, with no cap, and looks for one at arrivals that grow by an eighth (at least 16), so a proof that an idle callback finishes late is still used. The number of programs a function can have is then bounded by its blocks, not by four.

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

## HTTP fetch

`hosts/web/fetch.mjs` implements the `http_client` group ([Apps030](../design/Apps030.md#capability-groups)) with the browser's `fetch`: `new FetchHost({ origins })` is the `http` member of `browserHost`. The service is the one [Components](../design/Components030.md#http-clients) names. Two choices are recorded here: headers cross as one text list of alternating names and values, because the converter cannot pass a list of tuples to a host call (`E0801`), and the fourth argument is a timeout relative to the start of the request, not a deadline on a clock, because the page's monotonic clock is not the program's to share. `Std.Net.Http.Fetch` is the library binding ([below](#fetch-in-neura)).

| Argument | Meaning |
| --- | --- |
| method | An HTTP token. `DELETE`, `GET`, `HEAD`, `OPTIONS`, `POST` and `PUT` are sent in upper case whatever their case; `CONNECT`, `TRACE` and `TRACK` answer 4 |
| URL | Absolute, or relative to the page's address; `http` or `https` only. A URL with a user name or password answers 4 |
| headers | Names are HTTP tokens and values Latin-1 text without NUL, CR, LF, or surrounding spaces; the browser drops the headers the Fetch standard forbids (`Cookie`, `Host`, `Origin` and others) without an error |
| body | Empty for none; a body on a `GET` or `HEAD` answers 4 |
| timeout | Nanoseconds from the start of the request to the end of the response body; 0 for none, negative answers 4 |
| credentials | 0 `omit`, 1 `same-origin` (the browser's default), 2 `include` |

The answer is the response's status, its headers in the browser's order (lowercase names; for a cross-origin response only the CORS-safelisted ones and those the server lists in `Access-Control-Expose-Headers`), and its body after redirects. Any status is a success, 404 and 500 included: an error means there is no response.

| Code | When |
| ---: | --- |
| 4 | The request is malformed, as in the table above |
| 12 | The timeout passed; the exchange is aborted |
| 13 | The scheme is not `http` or `https` |
| 14 | The page does not grant the group, or the URL's origin is neither the page's nor one of the bundle's connect origins |
| 15 | The calling task's scope was cancelled; the exchange is aborted |
| 16 | `TooLarge`: the request body, or the response body as it streams in, exceeds W-3 (8 MiB); the exchange is aborted |
| 17 | `Busy`: W-4 (16) fetches are in flight |
| 18 | `OutcomeUnknown`: the browser reports a network failure, a CORS refusal, a policy refusal, or a body that ended early. It does not say which, nor whether the server acted on the request |

The host's promise never rejects, so a failed exchange fails the call, never the run. `FetchHost` takes `body` and `inFlight` to change W-3 and W-4 within their maxima (256 MiB and 256), `origins: null` to leave the origin check to the browser and the page's policy, and `fetch` to substitute the function. Requests go out with `mode: "cors"` and `redirect: "follow"`; the browser's cache rules apply as for any `fetch`. Requests for the same URL may not overlap: Chromium holds a second request for a URL until the first has finished, so three tasks fetching one URL take three times as long as three fetching three URLs. A bundle grants the group with `--http-client` or `--connect ORIGIN` ([Web bundles](#web-bundles)).

The browser enforces CORS: a cross-origin backend must answer the request (and its preflight, for a method other than `GET`, `HEAD` or `POST`, or a header outside the safelisted ones, such as `Content-Type: application/json`) with `Access-Control-Allow-Origin` naming the page's origin, and, for credentials `include`, `Access-Control-Allow-Credentials: true`. Without them the call answers 18. A response the page cannot read is never delivered in part.

### Fetch in Neura

`import:Std.Net.Http.Fetch` is the binding. `request(method, url)` makes a `Request` with no headers or body, a 30-second timeout and `SameOrigin` credentials; `with_header`, `with_body`, `with_text`, `with_timeout` and `with_credentials` return changed copies, and `send()` returns `Result<Response>`. `get(url)` and `post_text(url, content_type, text)` are shorthands. A `Response` has `status`, `body`, `ok()`, `header(name)` (ASCII case-insensitive), `headers()` and `text()`.

<!-- example: check -->
```neura
let http = import:Std.Net.Http.Fetch;
let result = import:Std.Core.Result;

let load_projects = func -> string {
    return match (http.request("GET", "https://api.example.com/projects")
            .with_header("Accept", "application/json").send()) {
        result.Result.Ok(response) => response.ok() ? result.value_or<string>(response.text(), "") : "",
        result.Result.Err(problem) => "failed: " + http.failure_of(problem).to_string(),
    };
};
```

Errors with codes 1 to 15 are I/O errors of the same kind, as for every host service; 16, 17 and 18 are custom errors whose codes are `http.TooLarge`, `http.Busy` and `http.OutcomeUnknown`. `failure_of(error)` classifies either as a `FetchFailure`. In a program with tasks a fetch overlaps: the calling task waits and the other tasks run, and cancelling its scope aborts the exchange.

A native `neura run` does not serve `neura.http.fetch` yet; an image that calls it fails to bind there ([Not provided](#not-provided-in-the-browser-yet)). `tests/apps/web/fetch-check.py` runs the cases above from a bundle in headless Chromium and Firefox against a page origin and a second, CORS-enabled origin.

## Gesture actions (proposed)

*Proposed 2026-11-17 by autonomous agents, not implemented; awaiting owner confirmation and the `Std.Web` side.* [Components](../design/Components030.md#events) binds a prepared `web.GestureAction` with `on:click={ web.with_gesture(action, then) }`; the browser allows a clipboard write, a download or a file picker only inside the user's event. The host side extends the encodings to version 2, which a host accepts beside version 1; a batch that uses none of the additions keeps version 1 and its bytes.

| Addition | Encoding |
| --- | --- |
| Commit operation 10, `gesture` | The node, event text, `uint64` handler (the `then` handler; 0 detaches), `uint8` action, then its fields: 1 copy, the text; 2 download, the file name text, the media type text, and a `uint32` byte count and the bytes; 3 pick files, the `accept` text and a `uint8` multiple flag. Only in a version 2 batch |
| Event kind 17, gesture result | Its node and handler as for every record, then the `uint8` action, a `uint8` status (0 done, 1 refused by the browser, 2 cancelled, 3 too large), and for a picked file a `uint32` file count and, per file, its name text, media type text, and a `uint32` byte count and the bytes. Written in a version 2 batch; the host writes version 1 when a batch holds no such record |

The adapter performs the action synchronously inside the DOM listener of the bound event, before it queues anything, and queues the result record when the action ends: at once for a download, when the clipboard's promise settles for a copy, and when the files are read for a pick. Picked files count against W-3 together; above it the status is 3 and no bytes are sent. No action runs outside a user event, and no result is promised within the event.

## History services

`HistoryHost` in `ui.mjs` implements the `history` group. A path is absolute (it starts with `/`), at most 8 KiB, without control characters; another path answers error 4. With `mode: "hash"`, the default, the path is kept in the fragment (`#/parts/7`), so any static file host serves the page. With `mode: "history"` it is the URL's path and query below `base` (default `/`; `new HistoryHost({ mode: 'history', base: '/app/' })` keeps `/parts/7` at `/app/parts/7`), and the server must answer every application path below the base with `index.html` (the server rule of [web bundles](#web-bundles)). Paths are percent-encoded in the URL and decoded when read; a malformed escape is read as written.

`push` and `replace` change the URL without loading a page and answer the current path. `next` waits for a navigation the program did not make (back, forward, an edited fragment); navigations while nobody waits coalesce to the latest. A second waiting `next` answers 3, and a cancelled one 15. `link` (version 1, added in 0.3.0) answers the `href` of a link to the path in the page's mode: `#/parts/7` with hash routing, and the base followed by the path, `/parts/7` or `/app/parts/7`, with history routing. A program learns the routing mode only through `link`, so one image runs under either mode.

## Web bundles

`scripts/build-web-bundle.py --image app.nvr --id org.example.app --out dist/web` assembles the static directory of [Projects](../design/Projects030.md#bundles) from a built image and the browser runtime. It stands in for `neura build` of a `web` target until the project tools exist. `--source app.nra` runs `neura build` first (`--neura` names the executable). The stylesheets and assets that the build listed in `<image>.styles.json` ([Stylesheets](../apps/Css.md#build-output-and-order)) are copied at their paths, `styles/<sha256>.css` and `assets/<sha256>.<extension>`, after their digests and sizes are checked; the stylesheets are linked from `index.html` in the listed order, and only those are `initial`. [Your first Neura web app](../apps/WebApp.md) walks through it.

| File | Content |
| --- | --- |
| `index.html` | The content-security policy as a `meta` element, the mount element (`--mount`, default `app`), a JSON data block naming the image and `nvm.wasm` with their SHA-256, an import map whose `integrity` table gives the SHA-256 of every module the boot module imports, directly or not, the boot module with its `integrity`, a message for browsers without WebAssembly exception handling, and a failure banner |
| `bundle.json` | Canonical JSON (sorted keys, no whitespace): `schema` 1, `kind` `web`, `id`, `name`, `version`, `image`, `files` (path, SHA-256, byte length, media type, `initial`), `requires` (`browser_adapter` 1, `ui_host` 1), `capabilities`, `origins`, `limits` (`W-1`), `contracts`, `csp`, `routing`, `base` (the URL path of a history-routed bundle) |
| `boot.<sha256>.mjs`, `nvm.<sha256>.js`, `ui.<sha256>.mjs`, `dom.<sha256>.mjs`, `ui-wire.<sha256>.mjs`, `fetch.<sha256>.mjs`, `nvm.<sha256>.mjs` | The browser host, with each import renamed to the file's digest name |
| `nvm.<sha256>.wasm`, `app.<sha256>.nvr` | The runtime and the image |
| `styles/<sha256>.css`, `assets/<sha256>.<extension>` | The image's stylesheets and assets, when it has any |

The policy is `default-src 'none'; script-src 'self' 'wasm-unsafe-eval' 'sha256-…'; style-src 'self'; img-src 'self'; font-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'none'`, where the hash allows the inline import map and nothing else, and the same text is the `csp` field. `--connect ORIGIN` (repeatable) adds an origin, a scheme, host and optional port, to `connect-src`, to `origins.connect` of `bundle.json`, and to the origins the page's `FetchHost` allows; it grants `http_client`, which `--http-client` grants for the page's own origin alone. Without either, `neura.http.fetch` answers 14, and the script says so when the image imports it.

With `--routing history` (and `--base`, default `/`, the URL path the bundle is served under), the page's own references (the boot module, the stylesheets and the keys of the import map's `integrity` table) start at the base instead of `./`, because the page is also served at deep application paths such as `/parts/7`, where a relative reference would resolve below `/parts/`; the policy's `base-uri 'none'` rules out a `<base>` element. The boot configuration and `bundle.json` carry `base`. The server rule: the host serves the bundle's files at `<base><file>` and answers every other path below the base with `index.html`. The script also writes `404.html`, a copy of `index.html`, for static hosts that serve that file for unknown paths (GitHub Pages, for example), and prints the rule. With hash routing the references stay relative and the bundle works under any path.

The script reads the image's import table before it writes anything and refuses an image that imports a service outside the groups a `web` target may have ([Apps030](../design/Apps030.md#capability-groups)): only `neura.console.*`, `neura.process.exit`, `neura.clock.*`, `neura.entropy.fill`, `neura.ui.*`, `neura.history.*`, `neura.http.fetch` and `neura.bridge.call` pass, and an image with native code or callbacks (`NATV`, `CALB`) is refused too. The compiler's own check of reachable services, `E1027`, belongs to `neura build` of a `web` target and is not part of this script.

The boot module fetches the image and `nvm.wasm` with their `integrity`, and the import map makes the browser check every module it imports, so a changed file stops the start before any of it runs. It compiles `nvm.wasm` by streaming when the host sends `application/wasm` and from bytes otherwise, runs the image with `browserHost`, a `UiHost`, a `HistoryHost` and, when the bundle grants `http_client`, a `FetchHost`, and with the largest step limit (an application's event loop lasts as long as the page; the runtime's default of 100,000,000 steps for the whole run would stop a busy page), and sets `data-neura-state` on the root element to `running`, `completed`, `failed` or `unsupported`. A trap shows the failure banner with the trap's diagnostic. A changed module fails before the boot module runs, so the page shows no state. The boot module fetches the image and the runtime relative to its own URL, which is the bundle's directory, and gives the `UiHost` that directory as `assetBase`, so bundled media resolve against the bundle whatever the page's path.

`tests/apps/web/bundle-check.py` loads a bundle in headless Chromium and Firefox through `tests/apps/web/load-page.mjs`, which drives Chromium over the DevTools protocol and Firefox over WebDriver BiDi, with no package dependency. It waits in real time for a condition in the page, up to a deadline, and starts a browser that does not come up again after 2, 4 and 8 seconds, so a loaded machine slows the check instead of failing it.

## Not provided in the browser yet

- Recording and replaying a browser run. The design, a native trace file written by `nvm.wasm`, is in [Apps030](../design/Apps030.md#browser-traces-design-not-implemented).
- Gesture actions ([proposed above](#gesture-actions-proposed)), and a model host for `neura run`. The native runner binds the UI and history services and answers each with error 14, so an image that imports `Std.Web` loads natively and `web.mount` returns `Err`; it does not register `neura.http.fetch`, so a native run of an image that imports it fails to bind.

## Desktop host services

This page is the reference for the host services that the window host of a desktop application provides to its native child, and for the channel that carries them. It holds the rows of the [desktop design](../design/Desktop030.md) that are implemented; that note keeps the rules still proposed and the reasons. The child's side is [`Std.Desktop`](../lang/Stdlib.md#115-stddesktop); building and running an application is in [Desktop applications](../apps/Desktop.md).

*Status:* implemented in Neura 0.3.0 and qualified on Linux x86-64 under Xvfb with Electron 45.3.3, by autonomous agents, awaiting owner confirmation. Windows and macOS are not built or qualified.

### The channel

The host starts the child with pipes for its standard input and output; the child's standard error is its log. Every message is a frame: a 4-byte little-endian length that counts the kind byte and the body, a 1-byte kind, and the body. A frame, its length field included, is at most D-3 (1 MiB). Integers in a body are little-endian; a text is a `uint32` byte count and UTF-8 bytes.

| Kind | Byte | Direction | Body |
| --- | --- | --- | --- |
| `Hello` | 1 | both | `uint32` protocol version (1), the 32 raw bytes of the SHA-256 of `registry.json` |
| `Call` | 2 | host to child | `uint32` request, `uint32` window, `int64` deadline nanoseconds, operation text, request bytes |
| `Return` | 3 | child to host | `uint32` request, `uint32` status (0 or an error code), reply bytes (for an error, the UTF-8 message) |
| `Cancel` | 4 | host to child | `uint32` request |
| `HostCall` | 5 | child to host | `uint32` request, service text, the service's arguments |
| `HostReturn` | 6 | host to child | `uint32` request, `uint32` error code, the results |
| `Event` | 7 | host to child | `uint32` kind (0 `Closed`, 1 `Focused`, 2 `Blurred`, 3 `Resized`, 4 `MenuActivated`), `uint32` window, two `int64`, identifier text |

Both sides send `Hello` first. A different version or digest ends the application before any operation runs; the host allows 5 s for the exchange. The host ends the channel by closing the child's input after the last window closes or after `neura.desktop.quit`, allows the child 5 s to exit, and then kills it. A protocol failure kills the child at once. The child's requests, the host's calls and the replies overlap: identifiers pair them, and either side may have many outstanding.

### Error codes

A service answers 0 or one of the codes of [the host service table](../lang/Stdlib.md), with the additions of the application platform: 1 `NotFound`, 2 `PermissionDenied`, 4 `InvalidInput`, 6 `UnexpectedEnd`, 12 `TimedOut`, 13 `Unsupported`, 14 `CapabilityDenied`, 15 `Cancelled`, 16 `TooLarge`, 17 `Busy`, 100 `Other`. An unknown service answers 13.

### Services

Every service is version 1, in the capability group `desktop`. Arguments and results are written in order; a handle is a `uint32`.

| Service | Arguments | Results | Notes |
| --- | --- | --- | --- |
| `neura.desktop.window.open` | title, width, height, minimum width, minimum height (`uint32`, at most 65,535), route text | window handle | `Busy` past D-1; a route starts with one `/` |
| `neura.desktop.window.close`, `.focus` | window | none | `InvalidInput` for a window that is not open |
| `neura.desktop.window.set_title` | window, title | none | The page cannot replace the title |
| `neura.desktop.menu.set` | the menu's canonical JSON text | none | See below; a refused menu changes nothing |
| `neura.desktop.dialog.open_file` | window, suggested name, filters | chosen (`uint32` 0 or 1), grant handle, file name | Grants reading |
| `neura.desktop.dialog.save_file` | window, suggested name, filters | as above | Grants writing; the system confirms an overwrite |
| `neura.desktop.file.read` | grant, maximum count | bytes, empty at the end | At most what fits one frame; `PermissionDenied` on a write grant |
| `neura.desktop.file.write` | grant, bytes | count (`int64`) | Writes every byte; the file is replaced at the first write; `PermissionDenied` on a read grant |
| `neura.desktop.file.close` | grant | none | `InvalidInput` for a grant that is closed |
| `neura.desktop.notify` | title, body | outcome (`uint32`: 0 shown, 1 denied, 2 unsupported) | Electron on Linux reports shown or unsupported |
| `neura.desktop.clipboard.write` | text | none | `CapabilityDenied` without `desktop.clipboard_write` |
| `neura.desktop.clipboard.read` | none | text | `CapabilityDenied` without `desktop.clipboard_read` |
| `neura.desktop.data_directory` | none | path text | `$XDG_DATA_HOME/<id>` on Linux, created with mode 700 |
| `neura.desktop.quit` | status (`uint32`, at most 255) | none: the host closes every window and ends the channel | The application exits with the status |

Filters are a `uint32` count (at most 32), then for each a label text, a `uint32` count of extensions (at most 32) and the extension texts, each 1 to 16 ASCII letters and digits, or `*`. A suggested name is a file name, never a path. The host keeps at most 64 grants and closes them all when the application ends.

A menu is `{"items":[...],"schema":1}`. An entry is `{"kind":"separator"}`, `{"items":[...],"kind":"submenu","label":...}`, or `{"accelerator":...,"enabled":...,"id":...,"kind":"item","label":...,"role":...,"target":...}`, with a target of `"native"` (a `MenuActivated` event with the focused window, 0 when none) or `"window"` (the focused page's `next_host_event()`; dropped when no window has focus). Identifiers are `[a-z0-9._-]{1,128}` and unique; labels are 1 to 256 characters without control characters; roles are `copy`, `paste`, `cut`, `undo`, `redo`, `select_all`, `quit` and `about`, which the system performs; at most four levels and 512 entries. An empty list removes the menu; until the child sets one, there is none.
