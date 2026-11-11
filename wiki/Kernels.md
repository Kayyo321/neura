<!-- Generated from docs/runtime/Kernels.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Kernels

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

A kernel is a deterministic, pure C function that the Neura runtime can call in place of standard library code
written in Neura, which runs as a neuron circuit and is slow for coarse operations such as hashing a buffer or
sorting an array. The kernels live in [`src/kernels/`](https://github.com/Kayyo321/neura/tree/main/src/kernels) and build into the static library
`neura_kernels`. Each one replaces the body of a function of the standard library and keeps its results,
errors, and traps exactly; the Neura implementation stays as its test oracle.

**Status.** Every kernel below can be bound, and the generated modules `Std.Internal.Kernels.*` declare them.
The standard library calls them from `Std.Digest.Sha256` and `Std.Digest.Crc32`; every function of
`Std.Math.Math`; `Std.Collections.Sorting.sort_ascending`; `Std.Text.Strings` (searches, comparisons, trims,
`split`, `split_limit`, `lines`, replacements, ASCII case mappings, `scalar_count`, `hash`, and the error path of
`from_bytes`) and the `String` companion (comparisons, ASCII case comparison, `trim`, and the members built on
`Strings`); `StringBuilder.hash_code`; the integer companions' `to_string_radix` and `parse_radix`; and
`Std.Encoding.Utf8.validate`, `Base64`, and `Hex` ([Standard library, P2a](Stdlib-Design#p2a-kernel-bindings-in-source)).
Every other module runs its Neura code. The scalar hash kernels `neura.hash.combine`, `spread`, `i64`, `u64`, and
`f64` are not called by the library, because a kernel call costs more steps than the few operations of Neura code
they would replace; they are kept for completeness and as oracles. An image that calls a kernel is NVR 1.6
([NVR](NVR#kernels-16)), and the NVM runs the call as a state operation
([NVM](NVM#kernel-calls)).

## ABI rules

The authoritative text is the comment at the top of
[`src/kernels/neura_kernel.h`](https://github.com/Kayyo321/neura/blob/main/src/kernels/neura_kernel.h). In short:

- **Pure.** A kernel reads only its arguments and writes only the buffers, state, and result pointers it is
  given. It has no global mutable state, never allocates, never calls back into the runtime, and never reads the
  clock, the environment, the locale, or a file.
- **Self-contained.** No kernel calls a libm function or a locale-dependent libc function.
  `scripts/check-kernels.sh` fails if the archive has an undefined libm symbol. Floating point uses C's
  `+ - * /` and comparisons on IEEE 754 binary64 with round-to-nearest-even, compiled with
  `-ffp-contract=off` and never with `-ffast-math` or `-march=native`; the build requires `FLT_EVAL_METHOD`
  0. Every NaN that a float kernel returns is the canonical NaN `0x7FF8000000000000`.
- **Deterministic.** The same arguments give the same results and statuses on every platform. State kept
  between calls is a byte array with a fixed little-endian layout.
- **Charged in advance.** Each kernel declares a step charge as a formula over its arguments. The runtime
  evaluates it and charges it before the call, so a step limit fires at the same point in every execution
  mode. The charge counts the most work the arguments allow, and a kernel's worst-case work is within a
  constant factor of it ([Step charges](#running-a-kernel)).
- **Work budget.** A kernel whose work depends on the data beyond its argument sizes takes a `neura_work`:
  it may spend at most `budget` units, adds what it spends to `used`, and returns
  `NEURA_TRAP_WORK_EXHAUSTED` instead of exceeding the budget. The runtime charges `used` after the call. No
  current kernel needs one.

### Values

| C type | Kind | Meaning |
| --- | --- | --- |
| `neura_i8` ... `neura_u64`, `neura_f32`, `neura_f64` | scalar | A Neura integer or float of the same width |
| `neura_bool` | bool | A Neura `bool`: 0 is false; kernels return 0 or 1 |
| `neura_text` | text view | A Neura `string`, valid UTF-8, read-only |
| `neura_bytes` | bytes view | A `uint8[list]` or `uint8[]`, any bytes, read-only |
| `neura_view_T` | T view | A read-only array of T |
| `neura_mut_T` | writable T view | An array of T the kernel changes in place |
| `neura_out_text*` | text output | A buffer for new text, which becomes a `string` |
| `neura_out_bytes*` | bytes output | A buffer for a new `uint8[list]` |
| `neura_state*` | state | Bytes the caller keeps between calls, in the layout the kernel family documents |
| `neura_work*` | work budget | The budget of a data-dependent kernel |
| `neura_T*` for a scalar T | T result | Where the kernel stores an additional result |

An output buffer belongs to the caller. A kernel first works out the exact result length; when it exceeds the
capacity, the kernel stores the length and returns `NEURA_NEED_CAPACITY`, and the caller grows the buffer and
calls again without a second charge. Each output has a declared capacity formula that is always enough, so a
runtime that allocates it never sees that status.

### Formulas

A step charge or buffer size is a formula of decimal integers, parameter names, `+ - * /`, parentheses, and
`log2(x)` (the floor, with `log2(0) = 0`), `min(a, b)`, and `max(a, b)`. A view, output, or state parameter
stands for its length in elements; a scalar parameter for its value, a negative value counting as 0. The
arithmetic is unsigned 64-bit and saturating, and `x / 0` is 0. The generated table holds each formula as
text and as a postfix program for `neura::kernels::evaluate`.

### Statuses

A kernel that can fail returns a `neura_status`. `NEURA_ERR_*` statuses reach the Neura wrapper as the outcome
tuple of its binding (below), which the wrapper turns into an `Err` of `Std.Core.Result` with the offset the
kernel stores through its `error_offset` parameter; `NEURA_TRAP_*` statuses become a trap.
`neura_kernel_status_message` gives the exact message and `neura_kernel_status_error_kind` the `Error` variant.

## Binding a kernel

A bodyless function of a trusted standard library module binds a kernel by ending in
`kernel("<name>", <ABI version>)`, as a host binding ends in `host(...)`:

<!-- example: fragment: a member of a trusted standard library module; user code cannot bind a kernel -->
```neura
export let sha256 = func(bytes: uint8[list]) -> uint8[list] kernel("neura.digest.sha256", 1);
```

Only library source may bind a kernel (`E0780` elsewhere). Lowering looks the kernel up in the compiler's table
(`E0782` for an unknown name or ABI version), checks that the function's signature is the one the kernel's
parameter and result kinds give (`E0781`, which spells out the expected signature), and records the
`kernel.binding` attribute ([NeuraIR 1.10](NirFormat)). A call compiles to a kernel state operation, not
to a host request. `scripts/gen-kernels.py` writes one such declaration per kernel into
`stdlib/std/internal/kernels/<header>.nra`, the internal module `Std.Internal.Kernels.<Header>`, which only
library code may import; [`src/kernels/kernel_binding.hpp`](https://github.com/Kayyo321/neura/blob/main/src/kernels/kernel_binding.hpp) holds the same
rules for the compiler, the loader, and the runtime.

Each C parameter has one role:

| C parameter | Role | Neura side |
| --- | --- | --- |
| Integer, float, or `neura_bool` scalar | Argument | The Neura scalar of the same width and signedness: `int8` ... `uint64`, `float32`, `float64`, `bool` |
| `neura_text` | Argument | `string`, viewed in place |
| `neura_bytes` | Argument | `uint8[list]` (an array `uint8[]` also binds) |
| `neura_view_T` | Argument | `T[list]` or `T[]` |
| `neura_mut_T` without a buffer size | Argument | `T[list]` or `T[]`, written in place |
| `neura_mut_T` with a buffer size | Scratch | Nothing: the runtime supplies the scratch space |
| `neura_state*` | Argument | `uint8[list]` (or `uint8[]`) of exactly the declared size, written in place |
| `neura_out_text*` | Result | `string` |
| `neura_out_bytes*` | Result | `uint8[list]` |
| `neura_T*` named `error_offset` | Error offset | The second element of the outcome tuple |
| Any other `neura_T*` | Result | The scalar `T` |
| `neura_work*` | None | The kernel has no binding yet: the runtime does not supply a work budget |

The function's result is the kernel's value: its scalar C result, or its one result parameter, or `void` when
it has neither. A kernel with more than one value has no binding. A kernel with an `error_offset` returns the
**outcome tuple** `[uint32, int64, T]`, or `[uint32, int64]` without a value: the status, 0 for success or the
`NEURA_ERR_*` code, then the error offset (0 on success), then the value, which is zero or empty after an error.
The standard library wrapper turns the outcome into a `Result`, with the message of
`neura_kernel_status_message`. A `NEURA_TRAP_*` status is the program trap (reason 3) with the message of
`neura_kernel_status_message`, the same trap and text as the Neura code the kernel replaces.

With these rules every kernel of ABI generation 1 has a binding. Two shapes need care, and both are expressed
with storage the heap model already has. A writable view and a state are ordinary mutable arrays or lists that
the kernel writes in place, as a run of element writes would; the runtime copies them in and writes them back
only when the kernel succeeds, so a trapping kernel leaves them unchanged and two arguments never alias in C. A
state's contents are the module's private field (for example `Sha256`'s 112 bytes), and a state another module
corrupts traps with `invalid state: kernel state` instead of misbehaving.

## Running a kernel

A kernel exit runs in the NVM (see [NVM](NVM#kernel-calls) for the full contract):

1. Resolve the arguments. Text, an NVR byte string, and an `int64` or `uint64` array or list are viewed in place;
   any other array or list is packed into a buffer the state reuses.
2. Evaluate the step charge on the arguments' lengths and values and add it to `steps`, with the step limit
   check of a block step, before the kernel runs. Under `--profile` the steps count for the calling function.
3. Size each output from its buffer formula, run the kernel, copy writable arguments back, and allocate the
   result through the allocator that `ConstructText` and collection allocation use, with the same heap-cell
   accounting and collection schedule.

A kernel call is not a host request: no trace records it, `--replay` needs nothing for it, `--deny-native` and
the capability flags do not affect it, and it works in tasks, parallel items, fleets, and the static launcher.

**Step charges.** A charge is one step for the call plus one step per unit of work that takes no longer than an
average NVM block step on the reference evaluator, so a step limit never lets a kernel do more work than the
same number of block steps could. For the digests the unit is 64 bytes read: on the measuring host one SHA-256
compression took about 0.15 µs and CRC-32 over 64 bytes about 0.1 µs (plus about 0.06 µs to pack a list's
bytes), while an average block step of the replaced SHA-256 code took about 0.75 µs.

The work counted is the most the call can do with the arguments it is given, never what it happens to do, which
the runtime cannot know before the call:

- **Bounded by the arguments.** A charge counts the bytes or elements the call can read or write, plus the
  writable views the runtime copies: `s - from` for a search that starts at an offset, the window
  `min(s - from, limit + needle)` for `neura.text.index_of`, `min(s / target, limit)` copies of the replacement
  for `neura.text.replace`, and `n log n` for a sort. A call is never charged for bytes it cannot touch, and the
  charge is within a small constant factor of the work the call can do (`tests/kernels/kernel_tests.cpp`,
  suite `table`, checks representative charges against the elements touched).
- **Linear work.** A kernel's worst case must be linear in what it is charged for. The text searches use the
  two-way algorithm, linear in the text read plus the needle, so a pathological needle cannot make a search do
  quadratic work for a linear charge.
- **No repeated charges.** A library loop never calls a kernel on the same text over and over. `Strings.split`,
  `split_limit`, and `lines` find all separators with `neura.text.split`, one call to count and one to write the
  offsets; `Strings.index_of` and `index_of_from` call `neura.text.index_of` on windows that start at 128 bytes
  and double, so the charges of one search add up to about twice the distance it covers, and a loop that calls
  `index_of_from` after each occurrence is charged in proportion to the text.

**Versions.** A kernel's name and ABI version identify its signature and behaviour; a change to either takes a
new version or a new name once the kernel has been released. A step charge or buffer formula is part of the
runtime's table, not of the image, so a runtime that changes a charge charges every image by the new formula.
No release has shipped a kernel yet, so the kernels changed before release keep ABI version 1.

## Kernels

Each table gives a header's kernels: the name and ABI version, the parameters in order (the C function
is `neura_k_` and the name after `neura.` with `.` turned into `_`), the result, the step charge, and the
buffer sizes. The notes under each table come from the comments in the header.

### [`neura_digest.h`](https://github.com/Kayyo321/neura/blob/main/src/kernels/neura_digest.h)

| Kernel | Version | Parameters | Result | Step charge | Buffers |
| --- | --- | --- | --- | --- | --- |
| `neura.digest.sha256` | 1 | `bytes` (bytes view), `out` (bytes output) | status | `1 + bytes / 64` | `out`: `32` |
| `neura.digest.sha256_init` | 1 | `state` (state) | status | `1` | `state`: `112` |
| `neura.digest.sha256_update` | 1 | `state` (state), `bytes` (bytes view) | status | `1 + bytes / 64` | `state`: `112` |
| `neura.digest.sha256_finish` | 1 | `state` (state), `out` (bytes output) | status | `2` | `state`: `112`, `out`: `32` |
| `neura.digest.crc32` | 1 | `bytes` (bytes view) | u32 | `1 + bytes / 64` | none |
| `neura.digest.crc32_update` | 1 | `register_value` (u32), `bytes` (bytes view) | u32 | `1 + bytes / 64` | none |

- `neura.digest.sha256`: Sha256.of: the 32-byte hash of the bytes.
- `neura.digest.sha256_init`: new Sha256(): a fresh running digest.
- `neura.digest.sha256_update`: Sha256.update.
- `neura.digest.sha256_finish`: Sha256.finish: pads, ends the digest, and writes the 32-byte hash.
- `neura.digest.crc32`: Crc32.of: the checksum of the bytes.
- `neura.digest.crc32_update`: Crc32.update on the running register, which a Crc32 keeps without the final exclusive-or (initially 0xFFFFFFFF; value() is register ^ 0xFFFFFFFF). Returns the advanced register.

### [`neura_encoding.h`](https://github.com/Kayyo321/neura/blob/main/src/kernels/neura_encoding.h)

| Kernel | Version | Parameters | Result | Step charge | Buffers |
| --- | --- | --- | --- | --- | --- |
| `neura.encoding.utf8_validate` | 1 | `bytes` (bytes view) | i64 | `1 + bytes / 64` | none |
| `neura.encoding.utf8_check` | 1 | `bytes` (bytes view), `error_offset` (i64 result) | status | `1 + bytes / 64` | none |
| `neura.encoding.utf8_scalar_count` | 1 | `text` (text view) | i64 | `1 + text / 64` | none |
| `neura.encoding.base64_encode` | 1 | `bytes` (bytes view), `url` (bool), `padded` (bool), `out` (text output) | status | `1 + bytes / 48` | `out`: `4 * ((bytes + 2) / 3)` |
| `neura.encoding.base64_decode` | 1 | `text` (text view), `url` (bool), `padded` (bool), `out` (bytes output), `error_offset` (i64 result) | status | `1 + text / 64` | `out`: `3 * (text / 4) + 2` |
| `neura.encoding.hex_encode` | 1 | `bytes` (bytes view), `out` (text output) | status | `1 + bytes / 32` | `out`: `2 * bytes` |
| `neura.encoding.hex_decode` | 1 | `text` (text view), `out` (bytes output), `error_offset` (i64 result) | status | `1 + text / 64` | `out`: `text / 2` |

- `neura.encoding.utf8_validate`: Utf8.validate: -1 when the bytes are valid UTF-8, otherwise the offset of the first ill-formed sequence (the length of the longest valid prefix).
- `neura.encoding.utf8_check`: Strings.from_bytes without the copy: NEURA_OK for valid UTF-8, otherwise NEURA_ERR_INVALID_START_BYTE, NEURA_ERR_INVALID_CONTINUATION_BYTE, or NEURA_ERR_UNEXPECTED_END_OF_DATA with the offset that Utf8.validate returns.
- `neura.encoding.utf8_scalar_count`: Strings.scalar_count: the number of bytes that are not continuation bytes.
- `neura.encoding.base64_encode`: Base64.encode, encode_unpadded, encode_url, encode_url_unpadded: `url` selects the URL- and filename-safe alphabet, `padded` the final `=`.
- `neura.encoding.base64_decode`: Base64.decode and its three variants, strict as documented there. Errors: NEURA_ERR_INVALID_CHARACTER, NEURA_ERR_INVALID_PADDING, NEURA_ERR_UNEXPECTED_PADDING, NEURA_ERR_UNEXPECTED_END_OF_DATA, and NEURA_ERR_NONZERO_TRAILING_BITS, each with its offset.
- `neura.encoding.hex_encode`: Hex.encode: two lowercase digits per byte.
- `neura.encoding.hex_decode`: Hex.decode: either case. Errors: NEURA_ERR_INVALID_HEX_DIGIT at the digit, NEURA_ERR_ODD_HEX_DIGITS at the text's length.

### [`neura_hash.h`](https://github.com/Kayyo321/neura/blob/main/src/kernels/neura_hash.h)

| Kernel | Version | Parameters | Result | Step charge | Buffers |
| --- | --- | --- | --- | --- | --- |
| `neura.hash.string` | 1 | `s` (text view) | u32 | `1 + s / 64` | none |
| `neura.hash.combine` | 1 | `seed` (u32), `value` (u32) | u32 | `1` | none |
| `neura.hash.spread` | 1 | `hash` (u32) | u32 | `1` | none |
| `neura.hash.i64` | 1 | `value` (i64) | u32 | `1` | none |
| `neura.hash.u64` | 1 | `value` (u64) | u32 | `1` | none |
| `neura.hash.f64` | 1 | `value` (f64) | u32 | `1` | none |
| `neura.hash.list_u8` | 1 | `items` (u8 view) | u32 | `1 + items / 64` | none |
| `neura.hash.list_i64` | 1 | `items` (i64 view) | u32 | `1 + items / 16` | none |
| `neura.hash.list_u64` | 1 | `items` (u64 view) | u32 | `1 + items / 16` | none |
| `neura.hash.list_f64` | 1 | `items` (f64 view) | u32 | `1 + items / 16` | none |

- `neura.hash.string`: string.hash_code and Strings.hash: start with 0; for each byte, 31 times the hash plus the byte, wrapping.
- `neura.hash.combine`: Hash.combine: 31 * seed + value, wrapping.
- `neura.hash.spread`: Hash.spread: hash ^ (hash >> 16).
- `neura.hash.i64`: Hash.of_int64 and int64.hash_code: low 32 bits ^ high 32 bits.
- `neura.hash.u64`: Hash.of_uint64 and uint64.hash_code.
- `neura.hash.f64`: float64.hash_code: the bit pattern with every NaN replaced by the canonical NaN, folded like uint64.
- `neura.hash.list_u8`: hash_code of a uint8 array or list: start with 1; for each element, 31 times the hash plus the element.
- `neura.hash.list_i64`: hash_code of an int64 array or list, with the int64 element hash.
- `neura.hash.list_u64`: hash_code of a uint64 array or list.
- `neura.hash.list_f64`: hash_code of a float64 array or list.

### [`neura_integer_text.h`](https://github.com/Kayyo321/neura/blob/main/src/kernels/neura_integer_text.h)

| Kernel | Version | Parameters | Result | Step charge | Buffers |
| --- | --- | --- | --- | --- | --- |
| `neura.integer.format_i64` | 1 | `value` (i64), `radix` (i64), `out` (text output) | status | `2` | `out`: `65` |
| `neura.integer.format_u64` | 1 | `value` (u64), `radix` (i64), `out` (text output) | status | `2` | `out`: `64` |
| `neura.integer.parse_i64` | 1 | `text` (text view), `radix` (i64), `result` (i64 result), `error_offset` (i64 result) | status | `1 + text / 16` | none |
| `neura.integer.parse_u64` | 1 | `text` (text view), `radix` (i64), `result` (u64 result), `error_offset` (i64 result) | status | `1 + text / 16` | none |

- `neura.integer.format_i64`: Int64.to_string_radix.
- `neura.integer.format_u64`: UInt64.to_string_radix.
- `neura.integer.parse_i64`: Int64.parse_radix.
- `neura.integer.parse_u64`: UInt64.parse_radix.

### [`neura_math.h`](https://github.com/Kayyo321/neura/blob/main/src/kernels/neura_math.h)

| Kernel | Version | Parameters | Result | Step charge | Buffers |
| --- | --- | --- | --- | --- | --- |
| `neura.math.sqrt` | 1 | `x` (f64) | f64 | `1` | none |
| `neura.math.cbrt` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.hypot` | 1 | `x` (f64), `y` (f64) | f64 | `2` | none |
| `neura.math.exp` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.expm1` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.log` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.log1p` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.log10` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.log2` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.pow` | 1 | `x` (f64), `y` (f64) | f64 | `4` | none |
| `neura.math.sin` | 1 | `x` (f64) | f64 | `4` | none |
| `neura.math.cos` | 1 | `x` (f64) | f64 | `4` | none |
| `neura.math.tan` | 1 | `x` (f64) | f64 | `4` | none |
| `neura.math.asin` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.acos` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.atan` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.atan2` | 1 | `y` (f64), `x` (f64) | f64 | `2` | none |
| `neura.math.sinh` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.cosh` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.tanh` | 1 | `x` (f64) | f64 | `2` | none |
| `neura.math.to_radians` | 1 | `degrees` (f64) | f64 | `1` | none |
| `neura.math.to_degrees` | 1 | `radians` (f64) | f64 | `1` | none |

- `neura.math.sqrt`: Math.sqrt: correctly rounded square root; sqrt(-0.0) is -0.0.
- `neura.math.cbrt`: Math.cbrt: cube root (s_cbrt.c).
- `neura.math.hypot`: Math.hypot: the square root of x squared plus y squared, without undue overflow (e_hypot.c).
- `neura.math.exp`: Math.exp: e to the x (e_exp.c).
- `neura.math.expm1`: Math.expm1: e to the x, minus 1 (s_expm1.c).
- `neura.math.log`: Math.log: natural logarithm (e_log.c).
- `neura.math.log1p`: Math.log1p: log(1 + x) (s_log1p.c).
- `neura.math.log10`: Math.log10: base-10 logarithm (e_log10.c).
- `neura.math.log2`: Math.log2: base-2 logarithm, exact for powers of two (after e_log10.c).
- `neura.math.pow`: Math.pow: x to the y (e_pow.c, 2004).
- `neura.math.sin`: Math.sin: sine (s_sin.c, with the reduction of e_rem_pio2.c and k_rem_pio2.c).
- `neura.math.cos`: Math.cos: cosine (s_cos.c).
- `neura.math.tan`: Math.tan: tangent (s_tan.c, k_tan.c 2004).
- `neura.math.asin`: Math.asin: arc sine (e_asin.c).
- `neura.math.acos`: Math.acos: arc cosine (e_acos.c).
- `neura.math.atan`: Math.atan: arc tangent (s_atan.c).
- `neura.math.atan2`: Math.atan2: arc tangent of y/x in the quadrant of (x, y) (e_atan2.c).
- `neura.math.sinh`: Math.sinh: hyperbolic sine (e_sinh.c).
- `neura.math.cosh`: Math.cosh: hyperbolic cosine (e_cosh.c).
- `neura.math.tanh`: Math.tanh: hyperbolic tangent (s_tanh.c).
- `neura.math.to_radians`: Math.to_radians: degrees to radians.
- `neura.math.to_degrees`: Math.to_degrees: radians to degrees.

### [`neura_sort.h`](https://github.com/Kayyo321/neura/blob/main/src/kernels/neura_sort.h)

| Kernel | Version | Parameters | Result | Step charge | Buffers |
| --- | --- | --- | --- | --- | --- |
| `neura.sort.i64` | 1 | `items` (writable i64 view), `scratch` (writable i64 view) | status | `1 + items * (1 + log2(items)) / 8` | `scratch`: `items` |
| `neura.sort.u64` | 1 | `items` (writable u64 view), `scratch` (writable u64 view) | status | `1 + items * (1 + log2(items)) / 8` | `scratch`: `items` |
| `neura.sort.f64` | 1 | `items` (writable f64 view), `scratch` (writable f64 view) | status | `1 + items * (1 + log2(items)) / 8` | `scratch`: `items` |

- `neura.sort.i64`: Sorting.sort of an int64 list with Int64.compare.
- `neura.sort.u64`: Sorting.sort of a uint64 list with UInt64.compare.
- `neura.sort.f64`: Sorting.sort of a float64 list with Float64.total_compare.

### [`neura_text.h`](https://github.com/Kayyo321/neura/blob/main/src/kernels/neura_text.h)

| Kernel | Version | Parameters | Result | Step charge | Buffers |
| --- | --- | --- | --- | --- | --- |
| `neura.text.index_of` | 1 | `s` (text view), `needle` (text view), `from` (i64), `limit` (i64), `result` (i64 result) | status | `1 + min(s - from, limit + needle) / 16` | none |
| `neura.text.last_index_of` | 1 | `s` (text view), `needle` (text view) | i64 | `1 + s / 16` | none |
| `neura.text.contains` | 1 | `s` (text view), `needle` (text view) | bool | `1 + s / 16` | none |
| `neura.text.starts_with` | 1 | `s` (text view), `prefix` (text view) | bool | `1 + prefix / 64` | none |
| `neura.text.ends_with` | 1 | `s` (text view), `suffix` (text view) | bool | `1 + suffix / 64` | none |
| `neura.text.compare` | 1 | `a` (text view), `b` (text view) | i32 | `1 + min(a, b) / 64` | none |
| `neura.text.count` | 1 | `s` (text view), `target` (text view), `result` (i64 result) | status | `1 + s / 16` | none |
| `neura.text.ascii_lower` | 1 | `s` (text view), `out` (text output) | status | `1 + s / 32` | `out`: `s` |
| `neura.text.ascii_upper` | 1 | `s` (text view), `out` (text output) | status | `1 + s / 32` | `out`: `s` |
| `neura.text.space_prefix_length` | 1 | `s` (text view) | i64 | `1 + s / 64` | none |
| `neura.text.space_suffix_start` | 1 | `s` (text view) | i64 | `1 + s / 64` | none |
| `neura.text.blank_prefix_length` | 1 | `s` (text view) | i64 | `1 + s / 64` | none |
| `neura.text.blank_suffix_start` | 1 | `s` (text view) | i64 | `1 + s / 64` | none |
| `neura.text.replace` | 1 | `s` (text view), `target` (text view), `replacement` (text view), `limit` (i64), `out` (text output) | status | `1 + s / 16 + min(s / target, limit) * replacement / 64` | `out`: `s + min(s / target, limit) * replacement` |
| `neura.text.split` | 1 | `s` (text view), `separator` (text view), `limit` (i64), `offsets` (writable i64 view), `result` (i64 result) | status | `1 + (s + offsets) / 16` | none |

- `neura.text.index_of`: One window of Strings.index_of_from (and index_of with from = 0): the first occurrence that starts in the window, at or after `from` and before `from + limit` (a limit below 1 counts as 1) moved forward to a character boundary. The result is the occurrence, or -1 when there is none in the rest of the text, or -2 - e when there is none in the window and later candidates remain, where e is the window's end, a boundary to search on from. An empty needle matches at `from`. `from` outside 0 through the length gives NEURA_TRAP_OFFSET_OUT_OF_RANGE and one on a continuation byte NEURA_TRAP_NOT_CHARACTER_BOUNDARY. The wrapper doubles the window on each call, so a search is charged in proportion to the distance it covers.
- `neura.text.last_index_of`: Strings.last_index_of: the last occurrence, or -1; an empty needle matches at the length.
- `neura.text.contains`: Strings.contains.
- `neura.text.starts_with`: Strings.starts_with.
- `neura.text.ends_with`: Strings.ends_with.
- `neura.text.compare`: Strings.compare as String.compare_to returns it: -1, 0, or 1 by byte-wise lexicographic order, a proper prefix first.
- `neura.text.count`: The number of non-overlapping occurrences, left to right: the number of replacements Strings.replace makes. An empty target gives NEURA_TRAP_EMPTY_TARGET, as Strings.replace traps.
- `neura.text.ascii_lower`: Strings.to_ascii_lower: A through Z become a through z.
- `neura.text.ascii_upper`: Strings.to_ascii_upper: a through z become A through Z.
- `neura.text.space_prefix_length`: The number of leading bytes of Strings.is_space (space, and 9 through 13). With e = space_suffix_start: Strings.trim is [min(this, e), e), trim_start is [this, length), and trim_end is [0, e).
- `neura.text.space_suffix_start`: Strings.trimmed_end: the offset after the last byte that is not a Strings.is_space byte, or 0.
- `neura.text.blank_prefix_length`: The number of leading bytes up to 32 (String.trim's set: every code point through U+0020). With e = blank_suffix_start: String.trim is [this, max(e, this)).
- `neura.text.blank_suffix_start`: The offset after the last byte above 32, or 0.
- `neura.text.replace`: Strings.replace (limit 2147483647) and replace_first (limit 1): replaces up to `limit` non-overlapping occurrences, left to right; a limit below 1 replaces nothing, as Strings.replace_up_to does. An empty target gives NEURA_TRAP_EMPTY_TARGET.
- `neura.text.split`: Strings.split, split_limit, and lines: the offsets of the first `limit` non-overlapping occurrences of the separator, left to right (none for a limit below 1). The offsets of the first ones are written to `offsets`, as many as it holds, and the result is the number found. The wrapper calls it with an empty list to count, then with a list of that length. An empty separator gives NEURA_TRAP_EMPTY_SEPARATOR.
