# `Std.DynamicForeign.SymbolLookup`

[Standard library index](Stdlib) · [Foreign interop](ForeignInterop) · [Language status](Status)

Import this module as `Std.DynamicForeign.SymbolLookup`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## Detailed contracts

[Foreign interop](ForeignInterop) defines the safety, determinism, and capability rules shared by every `Std.DynamicForeign` module. Native operations are granted by default and denied with `--deny-native`; when denied, the first one traps with `invalid state: native access denied: the native capability is not granted`.

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SymbolLookup** · [`export let SymbolLookup = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L18)
  A loaded library, the process's default lookup, or the image's embedded library. Tagged `#Final`, `#NotThreadSafe`.
- **SymbolLookup.SymbolLookup** · [`export let init = func(library_id: int64, scope: scopes.Scope)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L27)
  Use library_lookup, Linker.default_lookup, or embedded.
- **SymbolLookup.SymbolLookup** · [`export let find = func(name: string) -> Optional<MemorySegment>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L33)
  The address of `name` as a segment of size 0, or None when the library has no such symbol.
- **SymbolLookup.SymbolLookup** · [`export let find_or_trap = func(name: string) -> MemorySegment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L46)
  The address of `name`. Traps with "invalid argument: no symbol <name>" when it is missing.
- **SymbolLookup.SymbolLookup** · [`export let or_else = func(other: SymbolLookup) -> SymbolLookup`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L60)
  A lookup that tries this one, then `other` (Java's SymbolLookup.or). Tagged `#Pure`.
- **SymbolLookup.SymbolLookup** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L71)
  Names the lookup and its library identifier.
- **SymbolLookup** · [`export let library_lookup = func(path: string, arena: Arena) -> Result<SymbolLookup>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L80)
  Loads the library at `path`, or by name through the platform's search when `path` has no slash (such as "libm.so.6"), owned by `arena` (Java's SymbolLookup.libraryLookup). A library that cannot be loaded is an Io error of kind NotFound. On Windows a `path` that contains `/` or `\`, or starts with a drive such as `C:`, loads that file; any other name (such as "ucrtbase.dll") is searched in the application's directory, in `System32` and then in each `PATH` directory, never in the current directory. Any file extension is accepted.
- **SymbolLookup** · [`export let default_lookup = func -> SymbolLookup`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L97)
  The libraries already loaded into the process, including the C library. On Windows the lookup searches the executable, `ucrtbase.dll` (the C library), `kernel32.dll`, and then the other loaded modules in the order the system lists them. `ucrtbase.dll` does not export the functions that the C headers define inline, such as `printf`.
- **SymbolLookup** · [`export let embedded = func -> SymbolLookup`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/dynamic-foreign/symbol-lookup.nra#L100)
  The native library linked into the image from C imports (ForeignInterop.md).
<!-- END SOURCE API -->
