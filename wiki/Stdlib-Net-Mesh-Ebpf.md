# `Std.Net.Mesh.Ebpf`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.Ebpf`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.Ebpf`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ebpf** · [`export const MaxNameLength: int = 15`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L20)
  The longest program or map name in bytes (the kernel's 16-byte name with its NUL).
- **Ebpf** · [`export const InstructionBytes: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L22)
  The size of one eBPF instruction in bytes.
- **Ebpf** · [`export const MaxInstructions: int = 1000000`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L24)
  The most instructions a privileged loader accepts.
- **Ebpf** · [`export let AttachPoint = enum (  SockOps,  SkMsg,  SkLookup )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L27)
  Where a socket program runs.
- **Ebpf** · [`export let MapKind = enum (  SockMap,  SockHash )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L37)
  The kind of socket redirect map.
- **Ebpf** · [`export let EbpfProgram = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L46)
  An eBPF socket program: a name, an attach point, its license, and its bytecode. Tagged `#Final`.
- **Ebpf.EbpfProgram** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L48)
  The program name.
- **Ebpf.EbpfProgram** · [`export last attach_point: AttachPoint`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L50)
  Where it attaches.
- **Ebpf.EbpfProgram** · [`export last license: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L52)
  The license string the kernel checks for GPL-only helpers.
- **Ebpf.EbpfProgram** · [`export last instructions: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L54)
  The bytecode, 8 bytes per instruction, in the target's byte order.
- **Ebpf.EbpfProgram** · [`export let init = func(program_name: string, point: AttachPoint, program_license: string, code: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L57)
  A program (the bytecode is copied). Use `validate` before loading.
- **Ebpf.EbpfProgram** · [`export let instruction_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L69)
  The number of instructions. Tagged `#NoDiscard`.
- **Ebpf.EbpfProgram** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L72)
  Equal when the name, attach point, license and bytecode are.
- **Ebpf.EbpfProgram** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L89)
  Combines the name and the bytecode.
- **Ebpf.EbpfProgram** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L98)
  Text such as "EbpfProgram(redirect, SkMsg, 12 instructions)".
- **Ebpf** · [`export let RedirectMap = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L104)
  A socket redirect map description. Tagged `#Final`, `#Immutable`.
- **Ebpf.RedirectMap** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L106)
  The map name.
- **Ebpf.RedirectMap** · [`export last kind: MapKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L108)
  The map kind.
- **Ebpf.RedirectMap** · [`export last key_size: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L110)
  The key size in bytes (4 for a SockMap).
- **Ebpf.RedirectMap** · [`export last max_entries: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L112)
  The most entries.
- **Ebpf.RedirectMap** · [`export let init = func(map_name: string, map_kind: MapKind, key_bytes: int, capacity: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L115)
  A map description. Use `validate_map` before creating it.
- **Ebpf.RedirectMap** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L123)
  Equal when every field is.
- **Ebpf.RedirectMap** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L132)
  Combines the fields.
- **Ebpf.RedirectMap** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L135)
  Text such as "RedirectMap(socks, SockHash, key 12 bytes, 1024 entries)".
- **Ebpf** · [`export let LoadedProgram = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L141)
  A program loaded into the kernel, by its host handle. Tagged `#Final`, `#Immutable`.
- **Ebpf.LoadedProgram** · [`export last handle: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L143)
  The host's handle (a file descriptor on Linux).
- **Ebpf.LoadedProgram** · [`export last attach_point: AttachPoint`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L145)
  The program's attach point.
- **Ebpf.LoadedProgram** · [`export let init = func(host_handle: int64, point: AttachPoint)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L148)
  Wraps a host handle.
- **Ebpf.LoadedProgram** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L154)
  Equal when the handles are.
- **Ebpf.LoadedProgram** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L162)
  The low bits of the handle.
- **Ebpf.LoadedProgram** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L165)
  Text such as "LoadedProgram(7, SkMsg)".
- **Ebpf** · [`export let LoadedMap = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L171)
  A redirect map created in the kernel, by its host handle. Tagged `#Final`, `#Immutable`.
- **Ebpf.LoadedMap** · [`export last handle: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L173)
  The host's handle.
- **Ebpf.LoadedMap** · [`export last map: RedirectMap`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L175)
  The map description.
- **Ebpf.LoadedMap** · [`export let init = func(host_handle: int64, description: RedirectMap)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L178)
  Wraps a host handle.
- **Ebpf.LoadedMap** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L184)
  Equal when the handles are.
- **Ebpf.LoadedMap** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L192)
  The low bits of the handle.
- **Ebpf.LoadedMap** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L195)
  Text such as "LoadedMap(8, socks)".
- **Ebpf** · [`export let validate = func(program: EbpfProgram) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L201)
  Checks a program: a name of 1 to 15 bytes of letters, digits, `_` and `.`; a non-empty license without NUL; 1 to 1000000 whole 8-byte instructions. Tagged `#NoDiscard`.
- **Ebpf** · [`export let validate_map = func(map: RedirectMap) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L218)
  Checks a map: a valid name, at least one entry, and a key of 4 bytes for a SockMap or 1 to 512 bytes for a SockHash. Tagged `#NoDiscard`.
- **Ebpf** · [`export let load = func(program: EbpfProgram) -> Result<LoadedProgram>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L233)
  Validates and loads `program`. Needs the host's eBPF service (see the module). Tagged `#NoDiscard`.
- **Ebpf** · [`export let create_map = func(map: RedirectMap) -> Result<LoadedMap>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L240)
  Validates and creates `map`. Needs the host's eBPF service. Tagged `#NoDiscard`.
- **Ebpf** · [`export let attach_cgroup = func(program: LoadedProgram, cgroup_path: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L247)
  Attaches a SockOps program to the cgroup at `cgroup_path` (cgroup v2). Needs the host's eBPF service. Tagged `#NoDiscard`.
- **Ebpf** · [`export let attach_map = func(program: LoadedProgram, map: LoadedMap) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L256)
  Attaches an SkMsg program to a redirect map. Needs the host's eBPF service. Tagged `#NoDiscard`.
- **Ebpf** · [`export let attach_netns = func(program: LoadedProgram, netns_path: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L265)
  Attaches an SkLookup program to the network namespace at `netns_path` (such as "/proc/self/ns/net"). Needs the host's eBPF service. Tagged `#NoDiscard`.
- **Ebpf** · [`export let map_update = func(map: LoadedMap, key: uint8[list], socket_handle: int64) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L275)
  Stores the socket `socket_handle` under `key` in a redirect map. The key must have the map's key size. Needs the host's eBPF service. Tagged `#NoDiscard`.
- **Ebpf** · [`export let detach = func(program: LoadedProgram) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L286)
  Detaches and unloads a program. Needs the host's eBPF service. Tagged `#NoDiscard`.
- **Ebpf** · [`export let is_valid_name = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/ebpf.nra#L293)
  Whether `name` is 1 to 15 bytes of letters, digits, `_` and `.`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
