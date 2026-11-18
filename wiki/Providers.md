<!-- Generated from docs/runtime/Providers.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Providers: the desktop host services

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This page is the reference for the host services that the window host of a desktop application provides to its native child, and for the channel that carries them. It holds the rows of the [desktop design](https://github.com/Kayyo321/neura/blob/main/docs/design/Desktop030.md) that are implemented; that note keeps the rules still proposed and the reasons. The child's side is [`Std.Desktop`](Stdlib-Design#115-stddesktop); building and running an application is in [Desktop applications](Apps-Desktop).

*Status:* implemented in Neura 0.3.0 and qualified on Linux x86-64 under Xvfb with Electron 45.3.3, by autonomous agents, awaiting owner confirmation. Windows and macOS are not built or qualified.

## The channel

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

## Error codes

A service answers 0 or one of the codes of [the host service table](Stdlib-Design), with the additions of the application platform: 1 `NotFound`, 2 `PermissionDenied`, 4 `InvalidInput`, 6 `UnexpectedEnd`, 12 `TimedOut`, 13 `Unsupported`, 14 `CapabilityDenied`, 15 `Cancelled`, 16 `TooLarge`, 17 `Busy`, 100 `Other`. An unknown service answers 13.

## Services

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
