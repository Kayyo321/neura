# `Std.DynamicForeign.Arena`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.Arena`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations need `--allow-native`; without it the first one traps with `invalid state: native access denied: run with --allow-native`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Arena** · [`export let Arena = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L15)
  A lifetime for native memory.
- **Arena.Arena** · [`export let init = func(scope: scopes.Scope)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L20)
  Use of_confined or global.
- **Arena.Arena** · [`export let allocate = func(size: int64, alignment: int64) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L25)
  `size` zeroed bytes aligned to `alignment`, a power of two up to 4096.
- **Arena.Arena** · [`export let allocate_array = func(layout: MemoryLayout, count: int64) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L39)
  Zeroed memory for `count` consecutive values of `layout`.
- **Arena.Arena** · [`export let allocate_utf8 = func(text: string) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L48)
  The UTF-8 bytes of `text` followed by a NUL, as a C string. Traps if `text` contains a NUL.
- **Arena.Arena** · [`export let allocate_bytes = func(bytes: uint8[list]) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L55)
  A copy of `bytes` in native memory.
- **Arena.Arena** · [`export let is_alive = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L62)
  Whether the arena is still open.
- **Arena.Arena** · [`export let scope = func -> scopes.Scope`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L65)
  The scope shared with the arena's segments, for library code.
- **Arena.Arena** · [`export let close = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L69)
  Frees the arena's memory and unloads its libraries. Closing twice, or closing the global arena, traps.
- **Arena** · [`export let of_confined = func -> Arena`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L75)
  A new arena that frees its memory when closed (Java's Arena.ofConfined).
- **Arena** · [`export let of_shared = func -> Arena`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L78)
  The same as of_confined: Neura runs on one thread (Java's Arena.ofShared).
- **Arena** · [`export let global = func -> Arena`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/arena.nra#L81)
  The arena whose memory lives until the end of the run (Java's Arena.global).
<!-- END SOURCE API -->
