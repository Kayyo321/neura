# Desktop applications

This page is the reference for Neura desktop applications as implemented: the window host's application runner, the native child written with [`Std.Desktop`](../lang/Stdlib.md#115-stddesktop), bridge calls, windows, menus, dialogs and granted files, packages, and the measured budgets. The [desktop design](../design/Desktop030.md) keeps the rules not yet implemented and the reasons; the wire formats and host services are in [Providers](../runtime/Providers.md).

*Status:* Neura 0.3.0, qualified on Linux x86-64 under Xvfb with the pinned Electron 45.3.3, implemented by autonomous agents and awaiting owner confirmation. **No Neura component renders in a desktop window yet:** the renderer's NVM and components (`desktop.mount`) need the browser runtime and components of the application platform, so the pages of the tests are fixture scripts that call the bridge. Windows and macOS runners, `.dmg`, `.msix`, signing and `neura contracts` (generated dispatchers) are not implemented.

## Writing the native child

A desktop child is a program whose `main` returns `Std.Desktop.run_application(registry, start)`. `start` opens the first window and ends with `context.run(dispatcher)`:

<!-- example: check -->
```neura
let desktop = import:Std.Desktop;
let program = import:Std.Program;
let.promote _ = import:Std.Core.Result;
let.promote _ = import:Std.Core.Unit;

let Parts = module:program.Entry (
    // A bridge operation runs as its own task: it reaches the host through call.host() and call.window().
    let rename = func(call: desktop.Call) -> Result<uint8[list]> {
        match (call.window().set_title("Parts")) {
            Result.Ok(_) => { return Result<uint8[list]>.Ok(call.request()); },
            Result.Err(problem) => { return Result<uint8[list]>.Err(problem); },
        }
        return Result<uint8[list]>.Ok(call.request());
    };

    let start = func(context: desktop.Context) -> Result<Unit> {
        match (context.open_window("Parts", 960, 640)) {
            Result.Ok(_) => {},
            Result.Err(problem) => { return Result<Unit>.Err(problem); },
        }
        last dispatcher = new desktop.Dispatcher(new desktop.Registry(new(["parts.rename"])));
        dispatcher.add("parts.rename", rename);
        return context.run(dispatcher);
    };

    export let.override main = func(args: string[]) -> program.ExitCode =>
        desktop.run_application(new desktop.Registry(new(["parts.rename"])), start);
);
```

Build it with `neura build --native --deny-all --max-steps 18446744073709551615`, adding `--allow-clock` when operations use timers. The child lives as long as the application, so its policy must not keep the default step limit of `neura run`.

## Running an application

`runDesktopApplication` in `main.js` is the application runner, and `bootstrapDesktopPackage` its Electron entry: a package runs `main.js` with `desktop.json` beside it, and an unpackaged development run names the package with `--neura-package=<dir>`. `desktop.json` (schema 1, kind `desktop`) names the application id, the web bundle directory and the SHA-256 of its `bundle.json`, `registry.json` with its SHA-256, the child executable and arguments, the supported bundle requirements, `externalOrigins`, `capabilities` and `limits` (`D-1` to `D-4`, each defaulted by [Apps](../design/Apps030.md); `D-1` and `D-3` cannot exceed their defaults, which `Std.Desktop` keeps). Every path stays inside the package and names a regular file or directory. The child is the `neura build --native --deny-all --max-steps 18446744073709551615` executable of a program whose `main` returns `Std.Desktop.run_application`; the windows are opened by the child, never by the host. The step limit is raised because the child lives as long as the application: the default of `neura run`, 100,000,000 steps over the whole run, would end a long-lived application, and `tests/desktop/bridge_test.js` runs the real child past it.

`registry.json` is canonical: `{"operations":[...],"schema":1}` with distinct identifiers of `[a-z0-9._-]{1,128}` in ascending byte order, the text `Std.Desktop.Registry.json()` writes. Frame bodies, all integers little-endian and each text a `uint32` byte count and UTF-8 bytes:

| Kind | Body |
| --- | --- |
| `Call` | `uint32` request, `uint32` window, `int64` deadline nanoseconds, operation text, request bytes |
| `Return` | `uint32` request, `uint32` status (0 or an error code), reply bytes (for an error, the UTF-8 message) |
| `Cancel` | `uint32` request |
| `HostCall` | `uint32` request, service text, the service's arguments in the order of the table below (`uint32` handles and sizes, texts) |
| `HostReturn` | `uint32` request, `uint32` error code, the results (a `uint32` handle or outcome, or a text) |
| `Event` | `uint32` kind (0 `Closed`, 1 `Focused`, 2 `Blurred`, 3 `Resized`, 4 `MenuActivated`), `uint32` window, two `int64`, identifier text |

The page's `neuraDesktop.call(operation_id, bytes)` answers `{status, bytes}`. The status codes are those of the host service table with Backend's additions: 1 an operation outside `registry.json`, 2 a sender that is not the main frame of a runner window on `app://<id>`, 12 past D-4, 13 an operation without an implementation, 15 cancelled, 16 a Call frame over D-3, 17 the call past D-2 for the window, 100 the implementation's error. A refused call is logged and never reaches the child. `neura.desktop.quit` is answered by closing every window and ending the channel, not by a `HostReturn`; after the last window's `Closed` the host ends the channel too, and the child exits at the end of its input. The application's exit status is the status given to `quit`, 0 otherwise, and 1 with the failure report when the child fails, traps or exits with another status. Calls run concurrently in the child, as described below, and their replies come back in the order they finish. The pipe tests run with `python3 tests/desktop/run_app_test.py <electron> --neura <neura>`, which builds `tests/desktop/fixtures/app-child.nra`, runs `tests/desktop/bridge_test.js` and the Electron cases: `app-close`, `app-quit` and `app-trap` (cases 1, 2, 5, 15, 18, 23, 32), `app-services` (notifications, both clipboard directions, the data directory), `app-denied` (case 30, the host's refusal past a package's D-1), `app-windows` (Resized, the ninth window of case 31, child-initiated close, Focused and Blurred, cases 3, 19, 26 and 33) `app-dialogs` (cases 6 to 10 and 29: menus, the copy and paste roles, file dialogs through the scripted driver, granted files), `app-trap-dialog` (case 32 with the default failure report: the runner finds the native error dialog on the virtual display and dismisses it) and `app-killed` (case 34: the host is killed with SIGKILL and the child and the renderer end by themselves).

Every runner starts Electron under `xvfb-run` with `--ozone-platform=x11`, without `WAYLAND_DISPLAY`, `DISPLAY` and `XAUTHORITY` of the user's session, and with the session bus disabled; the Electron fixtures assert both. `xvfb-run` alone replaces only `DISPLAY`, and Electron 45 prefers Wayland when `WAYLAND_DISPLAY` is set, so a run that kept it opened its windows, dialogs and notifications on the user's own desktop. It also checks that a bridge operation capturing the `Context` is rejected (`tests/desktop/fixtures/capture-context.nra`, `E0602` at the program's call of `run`).

CTest registers these runners as `desktop-native-child`, `desktop-shell`, `desktop-app` and `desktop-package` (label `desktop`, run one at a time) when the build is configured with `-DNEURA_DESKTOP_ENGINE=<pinned electron executable>`; without it, or without Node and `xvfb-run`, they are not registered and configuration says so.

## Bridge calls in the child

`Std.Desktop` runs on the language's tasks ([Concurrency](../lang/Concurrency.md)):

- **The reader task.** `run_application` spawns one task that is the only reader of standard input. It reads through its own binding of the overlapping, cancellable `neura.console.read` version 2, not through `Std.Io.Console.input()`, whose read-ahead buffer belongs to the root task. It checks the host's `Hello`, answers each `HostReturn` to the request that registered its identifier, and passes `Call`, `Cancel` and `Event` frames to the application in order over a channel of 4,096 messages; messages beyond that wait in the reader. At the end of input it refuses new requests, answers every waiting request with `UnexpectedEnd`, and passes the end.
- **Host requests from any task.** Every request goes through a `Host`, a monitor that writes the `HostCall` frame (frames are written whole by one writer monitor, so frames from different tasks never interleave) and parks until the reader routes the answer. `Context` uses its own `Host`; each call gets another through `call.host()`, so one call's waiting dialog holds up no other call. A request of a cancelled task answers `Cancelled` and its late answer is dropped.
- **Calls.** `Context.run` spawns one task per `Call` frame. That task opens a scope, spawns the implementation in it, and waits for either the implementation or a `Cancel`; on a `Cancel` it cancels the scope, waits for the implementation to return, and writes no `Return`, because the host has already answered the caller. Request bytes reach the task as text when they are UTF-8 and as hex digits otherwise, because a byte list is not transferable.
- **What an implementation may do.** It is a spawned call, so it must be task-safe and capture only transferable values (numbers, text, `#Immutable` objects, monitors such as `Window`, `Host` and `GrantedFile`, and `Std.Concurrent` handles). It reaches the window host through `call.host()` and `call.window()`, never through a captured `Context`; capturing one is `E0602` at the program's call of `run`. Event handlers given to `Dispatcher.on_event` run on the task that called `run` and may use the context.
- **The end.** When the channel ends (the last window closed, `quit`, or the host gone), every call that has not reported finishing, a cancelled one included, is abandoned: the process ends at once with the application's status, with no scheduling point in between, because an operation that never parks and never asks would otherwise hold it forever with nobody waiting for its reply. When no call is left, `run` returns and `start` may continue. `run_application` ends the process with the application's status once `start` returns, so the reader task never keeps it alive.
- **Limits of this form.** A desktop child's image uses tasks, so it needs a runtime with the tasks NVR minor ([NVR](../runtime/NVR.md#scheduler-operations-15)), and, under the first concurrent image profile, it cannot use `Std.DynamicForeign` or `import:c(...)`. Timers in operations need `--allow-clock`; D-4 itself is enforced by the host, which sends `Cancel`, so the child needs no clock for it.

A renderer that ends abnormally (its process crashes or is killed; in future, its NVM traps) leaves its window open: the host logs the reason, cancels the window's calls in flight (the child receives `Cancel`, the caller nothing), and loads the window's start route once more, which the window's protocol handler answers with a static failure page that has a Reload link to that route. Following it is a fresh execution of the bundle. Electron 45 answers the clipboard asynchronously; the host awaits both clipboard services.

## Menus, dialogs and granted files

`neura.desktop.menu.set` takes the canonical JSON `{"items":[...],"schema":1}` that `Host.set_menu` writes: an entry is `{"kind":"separator"}`, `{"items":[...],"kind":"submenu","label":...}`, or `{"accelerator","enabled","id","kind":"item","label","role","target"}` with the target `"native"` or `"window"`. The host refuses (`InvalidInput`) an identifier outside `[a-z0-9._-]{1,128}` or used twice, more than four levels or 512 entries, a label that is empty, longer than 256 characters or holds a control character, an unknown role, or an accelerator outside Electron's form; the refused menu changes nothing. An empty list removes the menu. Until the child sets one there is no menu: Electron's default offers reloads and developer tools. Activating a `native` item sends `MenuActivated` with the focused runner window's handle (0 when none has focus); a `window` item goes to that window's page through `next_host_event()`, at most 64 waiting per window, and with no focused window it is dropped and logged (case 10).

The dialog services take a window handle, a suggested file name (a name, not a path) and up to 32 filters, each a label and up to 32 extensions of 1 to 16 letters and digits, or `*`; they answer a chosen flag, a grant handle and the chosen file's name. The host keeps at most 64 grants (`Busy` beyond). An open dialog grants reading of a regular file; a save dialog grants writing, and the file is replaced at the first write, so a grant never written leaves an existing file as it was. `file.read` answers at most what fits one frame; `file.write` writes every byte; `file.close` revokes. The host closes every grant when the application ends.

`runDesktopApplication` takes `dialogs: async (kind, {handle, suggestedName, filters}) => ({canceled, filePath})`, the scripted driver of the tests. A package built with `--test-hooks` names a JSON list of such answers in `desktop.json` (`testHooks.dialogs`), which the host follows in order; a release package has no `testHooks`, and the packager refuses a `desktop.json` that names them without `--test-hooks`.

## Packages

`python3 scripts/package-desktop.py --engine <electron directory> --package <package directory> --name <name> --icon <1024-pixel PNG> [--format tar.gz|deb|zip] [--test-hooks <dialog script>] -o <file>` assembles a package. The application tree is the engine with its executable renamed to `<name>` (`<name>.exe` for Windows) and without `default_app.asar`; `resources/app/` holding `package.json`, `main.js`, `preload.js` and the package directory; `resources/icons/` with the icon scaled from the one 1,024-pixel PNG (16 to 512 pixels, or a `.ico` of 16 to 256 for Windows); and `LICENSES/` with the engine's license, the Chromium notices, Neura's license, `PROVIDERS.txt` (the child's runtime and providers are Neura's; on Linux it links only the system C and math libraries, which the package does not carry) and the notices `desktop.json` lists under `notices`. The tool refuses a package directory that holds `.nra` source. Entries are sorted, owned by 0:0 and dated at `SOURCE_DATE_EPOCH` (default 0; a `.zip` records 1980-01-01), gzip records no name or time, and icons are encoded by Pillow without metadata, so the same inputs, Python and Pillow give the same bytes. The tool prints the archive's and the file tree's SHA-256 and says the package is unsigned.

| Format | What it is | Status |
| --- | --- | --- |
| `.tar.gz` | The tree, for Linux x86-64 | Built twice and compared, extracted under a path with spaces and non-ASCII characters and launched under Xvfb with no SDK on the path: the bridge retitles the window through the child, no process listens, SIGTERM ends it with 0 and no survivors |
| `.deb` | The tree under `/opt/<name>` (with `chrome-sandbox` setuid root, which Chromium uses where unprivileged user namespaces are off), `/usr/bin/<name>`, a desktop entry, hicolor icons and a copyright file; `Depends` lists Electron's libraries and `xdg-utils` | Built twice and compared, checked with `dpkg-deb -I` and `-c`, extracted with `dpkg-deb -x` as an installation root and launched as the `.tar.gz` is. Never installed into a system here, so installation and removal by `dpkg` are not qualified |
| `.zip` | The tree of a Windows x86-64 engine (a directory with `electron.exe`) | Only the container: built twice from a stand-in engine directory and compared and listed. Neither the Windows engine nor a Windows native child is available here, so no Windows package is built or qualified |
| `.dmg`, `.msix`, `--sign` | | Refused with a message naming the system that builds them; nothing is produced |

`--test-hooks` adds `test-dialogs.json` and names it in `desktop.json` (`testHooks.dialogs`); the host follows it in place of the system's dialogs, and the tool prints that the package is not a release. A release build refuses a `desktop.json` that names test hooks. `tests/desktop/run_package_test.py` runs all of the above. The fixture packages are about 127 MB, under B-D3.

`neura package --target <desktop target>` runs this tool on a project's desktop bundle and reports `E1033` for what it cannot build here ([Projects and bundles](Projects.md#packaging)); run directly, the tool ends with status 1 and a message.

## Budgets

`python3 scripts/bench-desktop.py <electron> --neura <neura> [--json <file>]` builds the fixture's `.tar.gz`, extracts it, launches it once to warm the file cache and then 10 times under Xvfb with `--timing`, which makes the host print the time from the start of its main process to the first window's `ready-to-show`. It reports that figure and the wall time from starting `xvfb-run` (which also starts an X server) to the line, and, 5 s after the first counted launch's first paint, the tree's PSS (the B-D2 measure) with the plain sum of RSS beside it for comparison, and the child's RSS; its JSON has `met` for each budget. The workload is the fixture (a static page and the Neura fixture child), not the reference application, which needs renderer-side Neura (A2, A3); the numbers are those of the host, an empty renderer and the child, and are not evidence about a Neura UI.

Measured on 2026-11-17, Linux x86-64 (Debian 13, kernel 7.1), AMD Ryzen 7 9800X3D on a shared, loaded machine (load average 5 to 15 during the run), Electron 45.3.3 under Xvfb, Release build:

| Budget | Measured | Limit | Met |
| --- | --- | --- | --- |
| B-D1 | 542.5 ms median, 560 ms 95th percentile from starting `xvfb-run`; 119.5 ms and 131 ms as the host reports it | 1.5 s median, 2.5 s 95th percentile | yes, for the fixture |
| B-D2 | 276.4 MiB tree PSS over 8 processes (each shared page counted once; the budget's measure, owner decision 2026-11-18); for comparison, 632.7 MiB as the plain sum of RSS, which counts each shared page in every process; child 36.4 MiB RSS | 350 MiB tree, 40 MiB child | yes for the tree; yes for the child, with 3.6 MiB left before the budget and no application state |
| B-D3 | 126,861,127 bytes (`.tar.gz`) | 130 MiB | yes |
