# Neura 0.3.0: desktop

This note fixes the desktop contracts before they are implemented: the window host, the renderer that shows the web target's bundle, the native Neura child, the typed bridge between them, windows, menus, dialogs and granted files, and packages for Windows, macOS and Linux. It is one of the notes of [the application platform](Apps030.md), which owns the matrix, the limits (`D-1` and the like), budgets and code ranges referred to here. Rows move to `docs/apps/Desktop.md` and `docs/runtime/Providers.md` in the change that implements them.

The owner approved the A0 design decisions on 2026-11-17. Rows marked *proposed* await implementation and qualification in A5. Later changes to a row are made here, with the reason.

The [desktop engine lock](../../hosts/desktop/engine.lock) records the approved Electron release and the official archive digests for the five platform and architecture pairs. The packaged engine carries its own license and Chromium third-party notices. The note adds no syntax.

```text
window host (Electron main process, shipped script, no application logic)
    |-- window 1: renderer = the web bundle, nvm.wasm, the frontend image
    |-- window 2: renderer = the same bundle, its own NVM and state
    `-- native child: the runner with the desktop image
            windows, menus, dialogs, granted files, bridge operations
```

<!-- example: fragment: the design form; named arguments, `?` and the exported `start` entry are not implemented, see Std.Desktop for the shipped form -->
```neura
let desktop = import:Std.Desktop;

export let start = func(context: desktop.Context) -> result.Result<unit.Unit> {
    last exporter = new PartsExporter(context);
    last window = context.open_window(title: "Parts", width: 960, height: 640)?;
    return context.run(parts_bridge_dispatcher(exporter));
};
```

## Processes and ownership

| Rule | Status |
| --- | --- |
| The window host is Electron 45.3.3 with two scripts the SDK ships, `main.js` and `preload.js`. They are platform code: they contain no application logic and are the same for every application. The author writes and installs no JavaScript and no Node. | proposed |
| The Neura-facing interface (`Std.Desktop`, the bridge, the host services below) names nothing of Electron, so a host built on a system web view can replace it later after its own qualification. | proposed |
| A renderer shows the web target's bundle, the same files with the same digests, loaded from the origin `app://<application id>/`. The host serves a file only after checking it against `bundle.json`, whose own digest is fixed in the package. | proposed |
| There is no HTTP server and no listening socket in a packaged application. | proposed |
| The native child is the runner executing the desktop image. The host starts it before the first window and it lives as long as the application. A desktop target's entry module exports `start: func(desktop.Context) -> Result<Unit>`. | partial: the child is a `build --native` launcher with the image embedded, and its `main` returns `Std.Desktop.run_application`; the exported `start` entry waits for project targets |
| Each window has its own renderer, its own NVM and its own state. Windows share nothing; they reach common state by calling bridge operations. There is no push channel in 0.3.0: an operation may wait for news up to `D-4`. | partial: each window has its own renderer and partition, but no renderer runs Neura yet (A2); the test pages are fixture scripts |
| Closing the last window, or `context.quit()`, ends the native child and every renderer. The host waits 5 s for the child, then kills it. Nothing is left running, and nothing is registered to start at login. | implemented (Linux x86-64, Xvfb) |
| A trap in a renderer ends that window's NVM. The host shows a static failure page with a reload control; a reload is a fresh execution. A trap in the native child ends the application with a native error dialog and a nonzero exit; it is not restarted. | partial: a renderer that ends abnormally (tested with the renderer killed, standing in for a trap, since no renderer runs Neura yet) shows the failure page, its calls are cancelled in the child, and Reload loads the route afresh; the child's trap ends the application with status 1 and the failure report, and the default report, the native error dialog, is tested on the virtual display (found, then dismissed) |
| Under `neura run --runtime=required` the whole tree is one application of the service. A packaged application needs neither the service nor the SDK. | proposed |

## Renderer isolation

| Rule | Status |
| --- | --- |
| Every window is created with Node integration off, context isolation on, the sandbox on, web security on, and developer tools off in a package built without `--dev-tools`. | proposed |
| The response headers of `app://` carry the bundle's content-security policy ([Projects](Projects030.md#bundles)). | proposed |
| Navigation of the main frame to anything but `app://<id>/` is cancelled. A link to an `https` origin listed under the target's `origins.external` opens in the system browser; any other is ignored and logged. `window.open` is denied. | proposed |
| Every permission request of the page (camera, location, notifications and the rest) is denied. Notifications and the clipboard are native-child operations. | proposed |
| `preload.js` exposes one object with two functions to the page: `call(operation_id, bytes)` and `next_host_event()`. There is no function that takes a channel name, a path, a command or a script. | implemented (Linux x86-64, Xvfb): `next_host_event()` answers `{kind: "menu", id}` for a `Window` menu item and `null` once the window closes; the host checks its sender as it does a call's |

The shell takes the web bundle directory, application ID and expected SHA-256 of `bundle.json` from trusted package metadata. In the schema 1 web manifest, `csp` carries the response policy, `image` has `path` and `digest`, and each `files` row has `path`, `sha256`, `bytes`, `media_type` and `initial`. Digests are lowercase hex. `requires` maps each required axis to its version. The shell compares these versions with the supported versions provided by the package loader. The desktop target's `origins.external` is supplied separately by that loader.

`hosts/desktop/main.js` exports `verifyWebBundle` for the package loader and `launchDesktopShell` for the trusted Electron entry. If the entry reads package metadata asynchronously before launch, it calls `registerDesktopScheme` first. The Linux shell fixture runs with `python3 tests/desktop/run_shell_test.py /path/to/electron`.

## Bridge

A bridge call carries the JSON bytes of a contract operation ([Components](Components030.md#contracts)).

| Rule | Status |
| --- | --- |
| The renderer's NVM calls the host service `neura.bridge.call` version 1, of the capability group `bridge`: operation id text, request bytes and a deadline in nanoseconds; it answers reply bytes or a `uint32` error code. It is overlapping and cancellable. A web target may list `bridge`; in a browser the service answers `capability_denied`, so one UI image serves both targets and chooses its transport from `web.Context.bridge()`, an `Optional`. | proposed |
| The window host checks, before the native child sees anything: the sender is the main frame of a window it created; that frame's origin is `app://<id>`; the operation id is in the package's `registry.json`; the frame is within `D-3`; the window is within `D-2`. A failed check is an error to the caller and a log record; the child is not called. | partial: registry, D-2 and D-3 checks are tested end to end; an embedded frame of the bundle gets no bridge object and a remote frame never loads, and the host's sender refusal is exercised with a synthetic IPC event from an embedded frame, because no page can send one |
| The host forwards the call to the native child with the window's identifier and a request identifier. The child's dispatcher, generated by `neura contracts`, decodes with the operation's codec, checks the contract version, calls the implementation, and encodes the reply. | partial: window and request identifiers are forwarded; the dispatcher is the hand-written `Std.Desktop.Dispatcher`, with no generated codec and no contract-version check (`neura contracts` does not exist) |
| At the first frame each side sends the bridge protocol version (1) and the digest of `registry.json`. A difference ends the application before any operation runs. | implemented (Linux x86-64, Xvfb) |
| A call that outlives `D-4`, or whose window closes, is cancelled in the child at its next cancellation point; a late reply is dropped. The caller gets `Err(TimedOut)` or nothing, never a retry. | implemented (Linux x86-64, Xvfb): each call runs as its own task, so a waiting call delays no other; a `Cancel` cancels the call's scope, which wakes a parked timer, channel or host request with `Cancelled`, and a computing operation stops at `Cancellation.requested()`; an operation that never parks and never asks runs to its end, and its reply is dropped |
| The host and the child speak over the child's standard input and output: frames of a 4-byte little-endian length, a 1-byte kind and a body, with the kinds `Hello`, `Call`, `Return`, `Cancel`, `HostCall`, `HostReturn` and `Event`. The child's standard error is its log. | implemented (Linux x86-64, Xvfb) |
| The implementation receives a `desktop.Call` with the window and nothing of the page: no URL, no script, no path. Browser input never selects a program to run, a file to open or a library to load. | implemented (Linux x86-64, Xvfb) |

The host-side channel in `main.js` counts the kind byte and body in the little-endian length. D-3 bounds the complete frame, including its four-byte length field. Version 1 assigns kind bytes 1 through 7 to `Hello`, `Call`, `Return`, `Cancel`, `HostCall`, `HostReturn`, and `Event` in that order. `Hello` has a four-byte little-endian version and the 32 raw SHA-256 bytes of `registry.json`. `Hello` travels both ways. The host writes `Call`, `Cancel`, `HostReturn` and `Event`, and reads `Return` and `HostCall`. Application bodies remain opaque at this transport boundary.

The trusted package bootstrap can pass `nativeChild` to `launchDesktopShell` once it has verified the installed runner, its launch configuration and the registry digest. `nativeChild` takes the absolute `executable`, `args`, optional `environment`, `registryDigest` in lowercase hex and optional `frameLimit`. The shell waits for the exchange before it creates a window. Startup has a five-second Hello deadline. A normal shutdown closes stdin and allows up to five seconds for the child to exit. A protocol failure terminates the child, and the host waits for process exit and closed streams before finishing. The channel accepts one pipe write at a time. The future typed dispatcher owns request IDs, per-window call limits, call deadlines and cancellation.

Run `node --test tests/desktop/native_child_test.js` for the pipe tests. The pinned Electron fixture accepts `--case child-mismatch`, `--case child-close`, `--case child-quit` and `--case child-crash`. Those cases use a separate test helper process supplied by the fixture. The packaged runner and desktop bundle loader are still needed for the application path.

### Implementation (A5)

The implemented rows of this note (the application runner, `desktop.json`, concurrent bridge calls in the child, menus, dialogs and granted files, packages and the measured budgets) are described in [Desktop applications](../apps/Desktop.md), and the channel and host services in [Providers](../runtime/Providers.md). Each row's status above says what is implemented, partial or proposed.

## Windows, menus and dialogs

All are operations of the native child, through host services of the `desktop` capability group. Every service is version 1 and answers a `uint32` error code on failure; types are written as in [Backend](Backend030.md#provider-abi).

| Service | Arguments | Success | Waits |
| --- | --- | --- | --- |
| `neura.desktop.window.open` | Title text, width, height, minimum width, minimum height (`uint32` each), route text | Window handle | no |
| `neura.desktop.window.close`, `focus` | Window handle | none | no |
| `neura.desktop.window.set_title` | Window handle, title text | none | no |
| `neura.desktop.menu.set` | The menu as canonical JSON text | none | no |
| `neura.desktop.dialog.open_file`, `save_file` | Window handle, suggested name text, filters (a list of a label text and extension texts) | A chosen flag, a file handle, the file's name text | yes |
| `neura.desktop.file.read` | File handle, maximum count (`int32`) | Bytes; empty at the end | yes |
| `neura.desktop.file.write` | File handle, bytes | Count written (`int64`) | yes |
| `neura.desktop.file.close` | File handle | none | no |
| `neura.desktop.notify` | Title text, body text | Outcome (`uint32`: shown, denied, unsupported) | no |
| `neura.desktop.clipboard.write` | Text | none | no |
| `neura.desktop.clipboard.read` | none | Text | no |
| `neura.desktop.event.next` | none | Kind (`uint32`), window handle, two `int64` values, an identifier text | yes |
| `neura.desktop.call.next` | none | Call handle, window handle, operation id text, request bytes, deadline time | yes |
| `neura.desktop.call.reply` | Call handle, reply bytes | none | no |
| `neura.desktop.data_directory` | none | Path text | no |
| `neura.desktop.quit` | Status (`int32`) | Does not return | no |

| Rule | Status |
| --- | --- |
| `context.open_window(title:, width:, height:, min_width:, min_height:, route:)` returns `Result<desktop.Window>`, up to `D-1` windows. `window.close()`, `set_title` and `focus` return `Result`. | partial: positional `open_window` and `open_window_at` until named arguments exist; `close`, `focus`, the child's ninth-window refusal and the host's refusal past a package's D-1 are tested end to end |
| `context.next_event()` yields `Closed(window)`, `Focused(window)`, `Blurred(window)`, `Resized(window, width, height)` and `MenuActivated(id, window)`, in the order the host observed them. `context.run(dispatcher)` consumes them with default handling and dispatches bridge calls until the application quits. | partial: Closed, Focused, Blurred and Resized are tested under Xvfb with no window manager and reach `Dispatcher.on_event` in the host's order; when focus moves, X reports the old window's Blurred and the new window's Focused in either order; MenuActivated is tested with a menu item activated by the test, standing in for a click |
| A menu is an immutable tree of records: an item has an `id`, a label, an optional accelerator, an optional standard role (`copy`, `paste`, `cut`, `undo`, `redo`, `select_all`, `quit`, `about`), an enabled flag, and a target, `Native` or `Window`. Activating a `Native` item is a `MenuActivated` event for the child; a `Window` item is delivered to the focused window's root as `RootEvent.Menu(id)`. Roles are performed by the system. | partial: `Host.set_menu` with `MenuItem` records (`action`, `role_item`, `separator`, `submenu`) is implemented and tested; a `Window` item reaches the focused page's `next_host_event()`, since no root runs Neura to receive `RootEvent.Menu` (A3); items are activated by the test, not by a pointer, and accelerators are passed to Electron and not exercised |
| `context.save_file_dialog(window:, suggested_name:, filters:)` and `open_file_dialog` return `Result<desktop.DialogOutcome<desktop.GrantedFile>>`: `Chosen(file)` or `Cancelled`. | partial: positional `save_file_dialog(window, suggested_name, filters)` and `open_file_dialog(window, filters)` on `Host` and `Context`, tested through the scripted dialog driver (cases 6 to 8); the system's own dialogs, and its overwrite confirmation of case 7, are not exercised by a test |
| A `GrantedFile` is the capability to read or write the one file the user chose, owned by `using` and revoked at `close()`. It exposes the file's name, never its directory. The host performs the reads and writes; the native child needs no `fs` capability, and the page receives only the operation's public reply. | implemented (Linux x86-64, Xvfb): `GrantedFile` implements `Std.Io.Closeable`, so `Closeable.release` closes it on every completion (the language has no `using`); a read grant only reads and a write grant only writes, and use after `close` is `InvalidInput` (case 29) |
| `context.notify(title:, body:)` returns `Shown`, `Denied` or `Unsupported`. `clipboard_write(text)` needs the manifest capability `desktop.clipboard_write`; `clipboard_read()` needs `desktop.clipboard_read`. Without the capability the service answers `capability_denied`. | partial: notify is tested through a scripted notification service (Shown and Denied) and through Electron's own (Shown under Xvfb); Electron on Linux never reports Denied, so `Denied` comes only from a scripted service there. Both clipboard directions are tested against the X display's clipboard, and both capability denials |
| `context.data_directory()` names the application's own directory (`$XDG_DATA_HOME/<id>`, `~/Library/Application Support/<id>`, `%LOCALAPPDATA%\<id>`); a SQLite pool declared for the desktop target lives there. Removing the package does not delete it, except where the system does. | partial: implemented and tested on Linux (`$XDG_DATA_HOME/<id>`, created with mode 700); the macOS and Windows paths are not run |
| An outgoing `neura.http.fetch` of the native child is performed by the host's network stack, within the origins the manifest lists. | proposed |

## Packages

| Rule | Status |
| --- | --- |
| A package contains: the engine; `main.js` and `preload.js`; the web bundle; the desktop image, the runner and the providers the manifest selects; `registry.json`; icons made from one 1,024-pixel PNG; and a `LICENSES` directory with the engine's and every provider's notices. It contains no `.nra`, no template or stylesheet source, no source map unless asked, and no SDK tool. | partial: `scripts/package-desktop.py` builds this tree with icons from one 1,024-pixel PNG and a `LICENSES` directory (engine, Chromium, Neura, `PROVIDERS.txt`, the notices `desktop.json` lists); the providers are Neura's own, so there is no other provider notice to carry ([Desktop applications](../apps/Desktop.md#packages)) |
| Formats are those of [the matrix](Apps030.md#desktop): `.zip` and `.msix`; `.app` in `.dmg`; `.tar.gz` and `.deb`. | partial: `.tar.gz` and `.deb` are built and launched here; the `.zip` container is built from a stand-in engine only; `.dmg` and `.msix` are refused |
| `.zip`, `.tar.gz` and `.deb` can be built on any SDK host that has the target component for that system installed. `.dmg` is built on macOS and `.msix` on Windows. A request the host cannot satisfy is `E1033`, naming what is missing. | partial: `neura package` reports `E1033` for `.dmg`, `.msix`, `--sign`, a missing icon or engine ([Projects and bundles](../apps/Projects.md#packaging)) |
| The unsigned content of a package is reproducible: the same project, lock and components give the same file tree and the same `.zip`, `.tar.gz` and `.deb` bytes (sorted entries, fixed times and owners). `neura package --report` prints the tree's digest for the formats whose container is not byte-stable. | partial: two builds of the `.tar.gz`, the `.deb` and the `.zip` container are byte-identical; the tool prints the tree's digest for every format |
| Signing is a separate, explicit step on the target system: `neura package --sign` runs `codesign` and `notarytool`, or `signtool`, with the identity the manifest names and credentials from the environment. Without `--sign` nothing is signed and the report says so. | partial: nothing is signed and the report says so; `--sign` is refused here |
| There is no updater. A package carries its version; installing a newer one replaces the older. Updating the SDK or the runtime service never touches an installed or running application. | proposed |
| The runner for each system is built on that system and delivered in its target component. The Windows runner needs only pipes, clocks and entropy from the system, because windows, files and fetches go through the host; the services that answer `unsupported` on Windows today stay so. | proposed; the child now relies on tasks and on an overlapping console read of a pipe, which are tested on Linux only ([Concurrency](../lang/Concurrency.md#16-limits)) |

## Lowering and artifacts

No NeuraIR, NVR or tier change. The desktop image is an ordinary image; the renderer's image is the web target's. The versions of the bridge protocol and of each `neura.desktop.*` service are independent axes ([overview](Apps030.md#version-axes)).

## Diagnostics

This note assigns no code. A desktop target meets `E1023` (its `ui` does not name a web target), `E1024` (no `start`), `E1027` (a capability used and not declared), `E1033` (a package that cannot be built here) and `E1045` (a bridge client for an unlisted operation), owned by [Projects](Projects030.md#diagnostics) and [Components](Components030.md#diagnostics). Everything else is a run-time outcome: an `Err`, an error code, a refused launch or a trap.

## Edge cases

Accepted:

1. An application that opens one window and has no bridge operations.
2. Two windows on different routes; closing one leaves the other and the child running.
3. A window reopened after being closed: a fresh NVM and fresh state.
4. A bridge call that waits 20 s for news and returns.
5. Sixteen bridge calls in flight from one window, answered out of order.
6. A save dialog cancelled: `Ok(Cancelled)`, no file touched.
7. A save dialog confirmed over an existing file: the system's own confirmation, then `Chosen`.
8. A `GrantedFile` written twice before `close`; a read grant used for reading only.
9. A menu with `copy` and `paste` roles working in a text input with no Neura code involved.
10. A `Window` menu item with no focused window: dropped.
11. An install path with spaces and non-ASCII characters; a user name with the same.
12. A launch with the network disabled: every asset loads.
13. A launch with no SDK, no `neurad` and no Node on the machine.
14. The same `bundle.json` digest in the web build and inside the package.
15. A 64-bit integer of 9,007,199,254,740,993 through a bridge call and back, exact.
16. `notify` on a system where notifications are turned off: `Ok(Denied)`.
17. A second launch while the first runs: a second, independent application.

Rejected or failed:

18. A page script calling an operation id that is not in the registry: an error to the caller; the child is not called.
19. A frame from an embedded or remote document calling the bridge: refused by sender and origin.
20. A navigation to `https://elsewhere.example/` in the main frame: cancelled.
21. A link to an origin not in `origins.external`: ignored and logged.
22. `window.open(...)`: denied.
23. A bridge frame of `D-3` plus one byte: `Err(TooLarge)`; nothing forwarded.
24. The 33rd call in flight from one window under the default `D-2`: `Err(Busy)`.
25. A call past `D-4`: `Err(TimedOut)`; its late reply is dropped.
26. A window closed with a call in flight: the call is cancelled in the child.
27. A registry digest that differs between host and child: the application ends at start with a message naming both.
28. A bundle file changed after packaging: the host refuses to serve it and the window shows the failure page.
29. A `GrantedFile` used after `close`: `Err(InvalidInput)`.
30. `clipboard_read()` without the capability: `Err(CapabilityDenied)`.
31. The ninth window under the default `D-1`: `Err`.
32. The native child traps: an error dialog, every window closes, exit status nonzero, no process remains.
33. A renderer traps during a bridge call: the failure page; the call's reply is dropped; the child continues.
34. The host is killed: the child sees its input close and exits; the renderers end with the host.
35. `neura package --target desktop --format dmg` on Linux: `E1033`.
36. `neura package --sign` with no identity in the environment: `E1033`, and no unsigned package is produced under a signed name.
37. A desktop target whose `ui` names a server target: `E1023`.

## Order of work

Each step is one commit with its targeted tests. None changes any image.

| Step | Content | Tests |
| --- | --- | --- |
| 1 | This note; the engine pinned with its digests and licenses in `hosts/desktop/engine.lock` | Document checks |
| 2 | `hosts/desktop/main.js`, `preload.js`: the `app://` protocol with digest checks, isolation settings, navigation and permission denial | Under Xvfb with the DevTools protocol: cases 12, 19 to 22, 28 |
| 3 | The native child on Linux and macOS: the stdio protocol, `neura.desktop.*`, `Std.Desktop` windows and events | Cases 1 to 3, 31, 32, 34; process-tree checks |
| 4 | The bridge: `neura.bridge.call`, host checks, the generated dispatcher, the registry handshake | Cases 4, 5, 15, 18, 23 to 27, 33 |
| 5 | Menus, dialogs, granted files, notifications, clipboard, with a scripted dialog driver that only a `--test-hooks` package honors | Cases 6 to 10, 16, 29, 30 |
| 6 | The Windows runner: a hosted Windows job, the runner built there, the stdio channel on pipes, the Windows wait source | The step 3 and 4 tests on Windows |
| 7 | `neura package` for `.tar.gz`, `.deb`, `.zip`; reproducibility; licenses; icons | Cases 11, 13, 14, 35; two builds compared byte for byte |
| 8 | `.dmg` and `.msix` on their runners; signing hooks | Cases 36; install, launch and remove on each runner |
| 9 | The launch, memory and size budgets | B-D1 to B-D3 (Linux measured with the fixture, above) |

## Gate and acceptance

- The package of each row of the matrix installs on a clean machine with no SDK, no service and no Node, launches offline, shows the reference UI from the same bundle digest as the web build, and removes cleanly.
- No process of the application listens on any socket (checked from the system's socket table), and none survives its exit.
- An unlisted operation, a remote frame, an oversized frame and a mismatched registry are each refused before the native child runs anything.
- The export journey of the reference application: save, cancel, overwrite, a denied destination; the page never receives a path.
- Windows, menus and dialogs are exercised by the scripted driver on every system, and by a person once per system for what a driver cannot see.
- Budgets B-D1 to B-D3 are met per system, or the row says why not.

Linux x86-64 is qualified on the development machine under Xvfb. Windows, macOS and arm64 Linux are qualified only by their hosted runners and by a person; until a row has both, it is reported as unqualified.

## Migration

A 0.2 program has no desktop form to migrate. A prototype that served its UI from a local HTTP server loads the bundle from the application origin instead, and its endpoints become bridge operations of a contract. A prototype that passed file paths to the page passes a public reply and keeps the file behind a `GrantedFile`.

## Decisions taken without the owner

*Proposed* 2026-11-17, implemented by autonomous agents, awaiting owner confirmation.

1. A desktop child cannot use `Std.DynamicForeign` or `import:c(...)`: concurrent calls need tasks, and the first concurrent image profile excludes native bindings. SQL and the other providers are host services and are not affected.
2. Bridge operations run as tasks, so an implementation must be task-safe with transferable captures, and reaches the host through `call.host()`. A `Call`'s `window` is a method, `call.window()`, because a monitor's other monitors are reached through methods.
3. When the channel ends with calls that have not reported finishing, a cancelled call included, the child ends at once with the application's status rather than waiting for operations that never ask whether they were cancelled; those calls are abandoned without a chance to finish cooperatively.
4. A cancelled call writes no `Return`: the host has answered its caller already, and dropping a late reply is its rule anyway.
5. Request bytes cross into a call's task as text when they are UTF-8 and as hex digits otherwise.
6. A desktop child is built with an unlimited step limit (`--max-steps 18446744073709551615`).
7. The `.deb` installs `chrome-sandbox` setuid root, as Electron's own packages do, for systems without unprivileged user namespaces.
8. There is no application menu until the child sets one: Electron's default menu offers reloads and developer tools.
9. A granted file is replaced at its first write, not when the save dialog closes, so a grant never written leaves an existing file as it was.
10. The host keeps at most 64 granted files, a menu has at most four levels and 512 entries, and a window keeps at most 64 unread host events.
11. `MenuActivated` carries window 0 when no runner window has focus.
12. When focus moves between windows, the old window's `Blurred` and the new window's `Focused` reach the child in the order X reports them, which may be either.
13. A `.deb` whose `desktop.json` names no maintainer says `Unknown <unknown@invalid>`.
14. B-D2 is measured as the PSS of the process tree (owner decision 2026-11-18): 276.4 MiB, within the 350 MiB budget. The plain sum of RSS (632.7 MiB), which counts every page the processes share once per process, is reported beside it for comparison only.
15. The notification service answers `Shown` when Electron accepted the notification; on Linux it does not learn whether one was displayed, and never answers `Denied`.

## Approved design decisions

1. The window host is Electron 45.3.3, with fixed scripts shipped by the SDK.
2. One UI bundle serves web and desktop; the bridge is a host service that a browser denies, so the transport is chosen at run time from an `Optional`.
3. Files, windows and outgoing fetches of the native child are performed by the host, which keeps the Windows runner's needs to pipes, clocks and entropy.
4. There is no push channel from the native side to a window; an operation may wait.
5. A trap in the native child ends the application; a trap in a renderer ends that window until the user reloads it.
6. Package formats are `.zip` and `.msix`, `.dmg`, `.tar.gz` and `.deb`; container formats that need a system's own tool are built on that system.
7. Signing is explicit and never implied; there is no updater.
8. Clipboard reads and writes are separate manifest capabilities.
9. Native dialogs are tested through a scripted driver that release packages ignore.
