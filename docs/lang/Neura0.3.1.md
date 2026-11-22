# Neura 0.3.1 Specification

Neura 0.3.1 extends the networking library and host providers introduced in 0.3.0. HTTP/3 transport, QPACK and MASQUE operations are deferred and return typed `Unsupported`. General QUIC, RPC, Realtime and Mesh remain in progress, and some cryptography providers remain subject to qualification.

The release aims to verify this memory-safety property end to end.

### "Every valid, safe Neura program must be unable to access memory outside a live, authorized allocation, regardless of its inputs or execution schedule."

The release also adds language features and one new standard library module, `Std.Core.Pointer`.

## Typed numeric literals

A numeric literal may contain `'` between digits, and it may end in a type suffix. The marks make a long value easier to read. The suffix fixes the literal's type, so the literal no longer takes the type of its context.

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let x = 120'000'000u64;
let y = 64'000s32;
let z = 100s16;
let w = 13.885f64;
```

`x` is `uint64`, `y` is `int32`, `z` is `int16`, and `w` is `float64`. A suffix names a width, not an alias: `s32` is `int32` (and therefore also `int`), and there is no suffix that means the names `int`, `byte`, or `float` by themselves.

| Suffix | Type |
| --- | --- |
| `s8`, `s16`, `s32`, `s64` | `int8`, `int16`, `int32`, `int64` |
| `u8`, `u16`, `u32`, `u64` | `uint8`, `uint16`, `uint32`, `uint64` |
| `f16`, `bf16`, `f32`, `f64` | `float16`, `bfloat16`, `float32`, `float64` |

An integer suffix applies to an integer literal. A float suffix applies to a floating-point literal, and also to a literal written with only integer digits, which is then a value of that float type (`1f32` is the `float32` value 1). An integer suffix on a literal that has a fraction or an exponent is a lexical error, as is an unknown suffix.

`'` is ignored when the compiler reads the value. A mark must sit between two digits of the same digit group: the integer part, the fraction, or the exponent. It must not start or end a group, repeat (`1''0`), or sit next to `.`, `e`, `E`, a sign, or the suffix. `120'000'000` is 120000000. The existing range and rounding rules still apply, so `300u8` and `-1u32` are compile-time errors, and a float suffix is rounded once into that format.

A literal with no suffix is unchanged: it takes the expected numeric type, or `int32` / `float64` when the context does not name one ([Numbers](Numbers.md)).

## If initializers

An `if` condition may open with one `let` declaration, a `;`, and then the condition. The declaration is evaluated once, before the condition. Its names are in scope in the condition, the `if` body, and the `else` body, and nowhere after the statement. The declaration may bind one name or destructure, and it follows the ordinary rules for `let`. The text after `;` is a condition under the same rules as a condition written on its own.

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let console = import:Std.Io.Console;

let bytes: uint8[] = [104, 105];

if (let [valid, text] = string.from_utf8(bytes); valid) {
    console.println(text);
}
```

`string.from_utf8` returns `[bool, string]`. The condition reads `valid` from that pair, and `text` is available only when the branch that uses it is inside this `if`. An `else if` may carry its own initializer. `while` does not take one.

## Nil conditions

A condition is a `bool`, or a class or interface reference. A reference is true when it refers to an instance and false when it is `nil`. `if (!val)` is the nil check: its body runs when `val` is `nil`, the same way `if (!pointer)` runs when `pointer` is `NULL` in C. `if (val)` runs only when `val` refers to an instance.

`not` and `!` are the same operator on a reference. `if (not val)` and `if (!val)` are the same nil check, and `not val` and `!val` give the same `bool` wherever they appear.

A nil condition narrows like a type test. Inside the body of `if (val)`, `val` is known to be non-nil. The same holds in the `else` body of `if (!val)` or `if (not val)`, and in the code after such an `if` when its body always leaves through `return`, `break`, `continue`, or a trap. Lowering records the proven use and its span in NeuraIR 1.16. A proven reference can use either ordinary member access or `?.` without a warning. Ordinary member access continues to execute the runtime nil check.

<!-- example: check -->
```neura
let Entry = class (
    export last message = "ready";
);

let message_or_empty = func(val: Entry) -> string {
    if (not val) {
        return "";
    }
    return val.message;  // val is non-nil here
};
```

Narrowing applies to a local, a parameter, or an immutable global binding that cannot change between the test and the use. A field, a reassignable module binding, a closure field, or a read that can invoke a class getter does not establish a fact about later reads. Assigning to the narrowed binding ends the narrowing. When execution can reach a read from more than one branch, each path must establish that the binding is non-nil.

`nil-access` warns when there is evidence of a nil value at a dereference. The diagnostic names the declaration or inferred return that can yield nil. Use `?.` when the result should be optional, or test the reference first. For a field, test a local copy, for example `if (let value = self.field; value) { ... }`.

A parameter without a nil default and a field or module binding with a written non-nil initializer promise a non-nil reference. `nil-argument` warns if a value that may be nil is passed or stored in one of these declarations. The compiler also tracks assignments to locals and summarizes possible nil results across calls. A value with an unknown nil state produces neither warning. [Nil warnings](CoreLanguage.md#nil-access-and-nil-argument-warnings) describes the declaration and coalescing rules.

Numbers, strings, enums, and other values that cannot be `nil` are still not conditions. `and` and `or` still require `bool`. A reference becomes a `bool` through `!`, `not`, or a comparison, and that `bool` can then be combined with `and` or `or`.

`Error` is a payload enum and cannot be `nil`. The test applies to a reference, such as a missing log entry:

<!-- example: check -->
```neura
let console = import:Std.Io.Console;

let LogEntry = class (
    export last message: string;

    export let init = func(text: string) {
        message = text;
    };
);

let val: LogEntry = nil;

if (!val) {
    console.println("nil");
}

if (val) {
    console.println("live");
}
```

`val` is `nil`, so `!val` is true, the first body prints `nil`, and the second body does not run. After `val` is assigned an instance, `if (val)` runs and `if (!val)` does not. A comparison such as `val == nil` remains an ordinary `bool`. A number, a string, an enum, and a `bool` keep the condition rules they already have.

### Nil defaults on generic references

A parameter of an applied class or interface type such as `Node<T>` may default to the literal `nil`. A bare type parameter such as `T` still takes no default. Other defaults on types that depend on a type parameter are `E0226`. See [Default values](CoreLanguage.md#default-values).

## Dot tags

A tag may be introduced with `.` instead of `#`. The two spellings mean the same thing in every position a tag is allowed ([Tags](Tags.md#syntax)): before a declaration, before an enum variant's name, and before a parameter's name.

```text
tags  := ( sigil tag | sigil '[' tag (',' tag)* [','] ']' )+
sigil := '#' | '.'
```

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
.Pure
export let area = func(width: int32, height: int32) -> int32 => width * height;

.[Deprecated(as_of: "0.3.1", reason: "use area"), NoDiscard]
export let size = func(width: int32, height: int32) -> int32 => width * height;
```

`.Pure` is `#Pure`, and `.[A, B(...)]` is `#[A, B(...)]`. Everything after the sigil is unchanged: tag names, arguments, groups, stacking, documentation comments among tags, and every rule about which declaration a tag may sit on. The two spellings may be mixed in one file and in one stack of tags. A tag written either way is the same tag to the compiler, so the checks, the diagnostics, NeuraIR, `neura docs` output, and the emitted image do not depend on the spelling.

No expression or statement starts with `.`, so a `.` where a declaration, a variant, or a parameter may begin always opens a tag. A `.` after an operand is still member access, and a member access continued on the next line (`value` on one line, `.name()` on the next) is unchanged.

Whatever is an error with `#` is the same error with `.`: a dot tag before a statement that takes no tags is `E0909`, and an unknown or misspelled tag name is reported as it is for `#`. A `.` at the start of a declaration that is followed by neither a tag name nor `[` is a syntax error at the `.`.

`neura fmt` keeps the sigil the author wrote: `--tag-style=keep` is the default on the command line and in the editor's formatter. `--tag-style=dot` rewrites every tag's sigil to `.` and `--tag-style=hash` rewrites every sigil to `#`. Each is an explicit conversion that changes sigils and nothing else. The standard library, the examples, and the tests are written with `.`, and dedicated fixtures keep the `#` spelling and mixed spellings under test. Diagnostics and generated documentation that print a tag keep printing it as `#Name` in 0.3.1.

## Laws

0.3.1 ships Phase 1 of [laws and mathematical proofs](../design/Laws031.md): module-level `law (...)` declarations with `| proof:` and `let.proof`, checked at compile time and erased before optimization and circuit conversion. Phases 2 to 4 of that design stay on the roadmap. The design page records the grammar, the proof rules, the NeuraIR revision, the diagnostics, and what 0.3.1 must demonstrate.

## Standard library completion and its exceptions

Neura 0.3.1 completes the first networking slice in `Std.Net.Core`, `Dns`, `Socket` and `Tls`. The release notes list the deferred cryptography providers below.

| Exception | Rule in 0.3.1 |
| --- | --- |
| PKCS #11 hardware security modules, TPM, cloud key management services, and live collection of enclave evidence | A backend is supported once it passes end-to-end qualification on its device or service. Until then its provider returns a typed `Unsupported`, with no silent software fallback. Coverage that comes from an emulator is labeled as emulator coverage. |
| FN-DSA | Deferred until the final FIPS 206 and matching conformance vectors are available. Key generation, signing and verification all return a typed `Unsupported` result while the implementation is deferred. |

The software keystore and offline verification of attestation evidence are supported and are not exceptions.

`Std.Net.Http` supports server body I/O, native HTTP/1.1 and HTTP/2 client exchanges, HTTP/2 frames and HPACK. See [HTTP build options and capabilities](../runtime/NVM.md#http-client-services). HTTP/3, QPACK and MASQUE transport operations return typed `Unsupported` in 0.3.1. General QUIC, RPC, Realtime and Mesh remain in progress for 0.3.1.

Direct TLS uses OpenSSL 4.1.0. Windows needs runtime verification for Winsock TLS and HTTP, guest-generation polling and the locked-memory provider. Generic secret locking protects the retained encoded representation with Linux page locking and dump exclusion or Windows `VirtualLock` with Windows Error Reporting memory exclusion. The original value and its aliases use ordinary program storage.

### DTLS 1.3 limitations

`Handshake.dtls13` uses the OpenSSL 4.1.0 DTLS listener with address cookies, peer certificate validation and an absolute deadline. The adapter moves datagrams through the caller's bound UDP socket. The upstream release lists these limitations:

- [#32878](https://github.com/openssl/openssl/issues/32878). An incoming KeyUpdate can clear other unacknowledged post-handshake records from the retransmission queue. Neura disables session tickets on this transport and exposes no session resumption API.
- [#33277](https://github.com/openssl/openssl/issues/33277). A permanent listener receive error can appear as a request for more input. Neura handles UDP socket errors in the datagram adapter, preserves failures on the OpenSSL error stack and bounds engine waits by the operation deadline.
- [#33278](https://github.com/openssl/openssl/issues/33278). A retryable listener write can be reported as a syscall error. Neura drains a full datagram BIO and retries when OpenSSL and the BIO both request a write retry and the error stack is empty.
- [#33242](https://github.com/openssl/openssl/issues/33242). A server can abort a handshake after receiving an early acknowledgment of its first flight. The application receives a handshake error.

[OpenSSL 4.1 release notes](https://openssl-library.org/post/2026-10-23-openssl-41-final-release/)


## Windows native build

From 0.3.1 on, Windows is a supported platform in its own right, not only through WSL 2. Every release publishes a native Windows archive. Every later release must keep it building, tested, and published, with the same gates as the Linux and macOS archives.

### Install

On Windows 10 and 11, one command in PowerShell installs Neura. It has the same copy-and-paste form as `install.sh`:

```powershell
irm https://github.com/Kayyo321/neura/releases/latest/download/install.ps1 | iex
```

`install.ps1` follows the same rules as `install.sh`:

- It downloads the archive for the machine's architecture from the release.
- It checks the archive's SHA-256 against the release metadata before unpacking it, and stops on a mismatch.
- It writes only under the install root, `%LOCALAPPDATA%\Neura` by default, and needs no administrator rights.
- It accepts the same choices as `install.sh`: a version or channel, the SDK or the runtime-only product, and the install root.

The one difference from `install.sh`: to keep install a single command, it adds the `bin` directory to the user's `PATH`, in the user environment only, never the machine environment. `-NoPathUpdate` skips that step. The script can be downloaded and read before it is run, as `install.sh` can. It must run under Windows PowerShell 5.1 and PowerShell 7.

After install, `neura update`, `neura uninstall`, the SDK/runtime split, and `neura doctor` behave as on Linux. An existing WSL install is unaffected.

### Targets and parity

- `x86_64-windows-msvc` is required, and it is the only spelling in Neura's release and update metadata. The Clang spelling `x86_64-pc-windows-msvc` stays accepted where it already is, at the foreign-function boundary. `aarch64-windows-msvc` is published when it passes the same gates. Otherwise it is listed as not yet supported.
- Every language feature, standard library package, and tool that exists on Linux exists on Windows. Where Windows needs a different host mechanism, the behaviour seen from Neura code is the same:
  - entropy: `BCryptGenRandom`;
  - locked memory: `VirtualLock`;
  - I/O readiness: IOCP or `WSAPoll`, instead of epoll and io_uring;
  - request workers: `CreateProcess` instead of the fork-based zygote;
  - file paths and line endings.

  A host feature that Windows cannot provide is a documented, typed `Unsupported` result. It is never a crash or a silent difference.
- Native tier-3 code generation either supports the Windows x64 calling convention or declines to tier 2 on Windows. Both paths must give identical results under the runtime-identity and tier-boundary gates.
- Apps:
  - `neura build --target web|server|desktop` and `neura run` work on Windows;
  - `neura package` produces a Windows desktop package (`.zip`, and `.msix` once trusted signing and a clean Windows install both pass), replacing today's E1033 refusal;
  - the browser runtime is tested in Edge and Chrome on Windows.
- The editor extension and the debugger work against the native Windows install.

### Verification

- The final 0.3.1 release requires verified Authenticode signatures on the Windows executables, DLLs, and installers ([code signing](../CodeSigning.md)). Implementation and release candidates may proceed unsigned while the signing application is under review.
- CI builds and runs the full test suite on a Windows runner for every pull request and release.
- `qualify.ps1`, or `qualify.sh` running under PowerShell, checks the Windows archive: contents, size budget, install, update from the previous release, and uninstall.
- The lifecycle tests run against `install.ps1`: a fresh install, an update, a rollback, an uninstall, a path with spaces and non-ASCII characters, and a missing or corrupted download.
- Manual qualification runs on a real Windows 10 or 11 machine before each final release: install with the one-line command, a desktop app, a server app, and the VS Code extension.

## `Std.Core.Pointer`

## 1. Overview

`Std.Core.Pointer` introduces typed, memory-safe pointer abstractions for applications that require explicit control over references, ownership, object lifetimes, collection views, and concurrent access.

Unlike traditional pointer systems, Neura pointers never expose unrestricted memory addresses. They operate exclusively through managed references, compiler-validated ownership operations, and runtime-checked access.

The library must guarantee that safe Neura code cannot produce:

- Dangling references or use-after-free.
- Out-of-bounds memory reads or writes.
- Double-free or invalid deallocation.
- Unsafe pointer arithmetic.
- Invalid pointer type reinterpretation.
- Use-after-move of exclusively owned values.
- Mutable reference escapes that violate ownership or synchronization.
- Data races through the library's cross-task APIs.

**Safety is mandatory.** No pointer API may offer an unchecked or unsafe variant, even as an opt-in feature.

A memory error must either be rejected at compilation or handled through a defined safe runtime failure. It must never produce undefined memory behavior.

### 1.1 Scope

This proposal introduces nine abstractions:

| Type | Purpose |
|---|---|
| `Pointer<T>` | Strong reference to managed mutable storage |
| `ConstPointer<T>` | Read-only reference to managed storage |
| `ListPointer<T>` | Bounds-checked collection views |
| `UniquePointer<T>` | Exclusive ownership and controlled mutation |
| `SharedPointer<T>` | Shared ownership of managed objects |
| `WeakPointer<T>` | Non-owning reference with safe upgrade |
| `SoftPointer<T>` | Garbage-collector-aware cache reference |
| `AtomicPointer<T>` | Atomic references to safely shareable objects |
| `LockedPointer<T>` | Synchronized access to mutable state |

These abstractions complement Neura's existing garbage collection, class references, primitive references, and concurrency mechanisms.

They do not replace the runtime's memory manager.

### 1.2 Implementation boundary

The declarations and usage examples in this document describe the proposed API using Neura syntax.

The pointer library does not yet exist. Operations that require ownership tracking, restricted borrowing, collector integration, or atomic reference handling require compiler and runtime support.

They must not be implemented as ordinary wrappers if doing so would weaken their stated guarantees.

---

# 2. Pointer Types

## 2.1 Pointer<escape><T></escape>

### Purpose

`Pointer<T>` is a managed reference to mutable storage containing a value of type `T`.

It allows multiple references to the same storage without exposing raw memory or requiring manual allocation and deallocation.

### Construction

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let cell = import:Std.Core.Cell;
let.promote _ = import:Std.Core.Pointer;

let storage = cell.of<int64>(10);
let p: Pointer<int64> = new(storage);
```

A `Pointer<T>` references a managed storage cell. It does not take ownership of an arbitrary variable's memory address.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `read()` | `T` | Reads the stored value |
| `write(value: T)` | `void` | Replaces the stored value |
| `as_const()` | `ConstPointer<T>` | Creates a read-only view, subject to type restrictions |
| `clone()` | `Pointer<T>` | Creates another reference to the same storage |

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let storage = cell.of<int64>(5);

let p: Pointer<int64> = new(storage);
let alias = p.clone();

let original = p.read();  // 5

alias.write(20);

let current = p.read();   // 20
```

### Safety guarantees

- The referenced storage remains alive while reachable through a live pointer.
- No operation exposes the storage's memory address.
- `clone()` cannot create a dangling reference.
- `write()` accepts only values of the declared type.
- A non-nil `Pointer<T>` always refers to valid managed storage.
- References to mutable storage cannot cross task boundaries unless an explicitly permitted synchronization mechanism provides safe access.

A `Pointer<T>` binding may itself be `nil` under Neura's existing class-reference rules. Dereferencing a nil pointer produces a defined runtime trap, never an unchecked memory access.

For reference-valued `T`, `read()` follows Neura's ordinary value semantics: it may return a managed object reference rather than a deep copy.

---

## 2.2 ConstPointer<escape><T></escape>

### Purpose

`ConstPointer<T>` provides read-only access to managed storage.

It prevents modifications through the pointer while retaining a safe, strongly reachable target.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `read()` | `T` | Reads the stored immutable value |
| `clone()` | `ConstPointer<T>` | Creates another read-only view |

No `write()`, `handle()`, mutable indexing, or mutable-borrow method is available.

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let cell = import:Std.Core.Cell;
let.promote _ = import:Std.Core.Pointer;

let sum = func(data: ConstPointer<int64>) -> int64 {
    return data.read();
};

let storage = cell.of<int64>(42);
let p: Pointer<int64> = new(storage);

let readonly = p.as_const();

let result = sum(readonly);  // 42

// readonly.write(100); // Compile-time error
```

### Deep immutability requirement

Preventing `write()` is not sufficient to make a pointer read-only.

For example, returning a mutable class instance would allow callers to change its fields even without calling `write()`.

Therefore, `ConstPointer<T>.read()` is available only when the compiler can prove that `T` is deeply immutable.

Initially, this includes:

- Primitive numeric types.
- Booleans.
- Strings.
- Immutable enums and tuples.
- Classes satisfying Neura's `#Immutable` requirements.

Mutable collections and ordinary mutable classes require a compiler-enforced read-only borrow mechanism before they can be exposed through `ConstPointer`.

The library must not introduce an ordinary mutable object reference as a substitute for a read-only borrow.

### Safety guarantees

- No mutation through the read-only capability.
- No mutable reference escape through `read()`.
- The storage remains alive while referenced.
- No casting from a read-only pointer into a mutable pointer.
- No removal of the declared immutability restrictions through generics or type conversion.

---

## 2.3 ListPointer<escape><T></escape>

### Purpose

`ListPointer<T>` is a memory-safe view over a contiguous region of a managed collection.

It supports indexing, mutation, and subviews without copying the underlying elements.

Unlike an unchecked array pointer, it never permits access outside the valid collection region.

### Construction

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let.promote _ = import:Std.Core.Pointer;

let data: int64[] = [10, 20, 30, 40, 50];

let view: ListPointer<int64> = new(data, 0, data.length);
```

The constructor receives:

- The managed collection.
- A start index, inclusive.
- An end index, exclusive.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `len()` | `int` | Returns the number of elements in the view |
| `value_at(index: int)` | `T` | Reads an element after bounds validation |
| `set(index: int, value: T)` | `void` | Writes an element after bounds validation |
| `sub(start: int, end: int)` | `ListPointer<T>` | Creates a checked subview |
| `is_empty()` | `bool` | Reports whether the view has zero elements |

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let data: int64[] = [1, 2, 3, 4, 5];

let view: ListPointer<int64> = new(data, 0, 5);

let middle = view.sub(1, 4);

let count = middle.len();       // 3
let first = middle.value_at(0); // 2

middle.set(1, 30);

let changed = data[2] == 30;   // true
```

### Subview semantics

A subview uses indices relative to its parent view.

For a parent containing five elements, `sub(1, 4)` exposes three elements.

A subview references the original collection rather than duplicating it.

Nested subviews must preserve the same bounds guarantees.

### Collection resizing

The initial implementation should support fixed-length Neura arrays.

Support for growable `T[list]` collections may be added with structural-version tracking.

When a backing collection changes size, an existing view must either:

- Remain valid because its entire region is still provably accessible, or
- Become invalidated and produce a defined runtime failure when accessed.

It must never retain stale offsets into released or reallocated storage.

### Safety guarantees

- Every index is bounds-checked.
- All subview ranges are validated.
- Index and length arithmetic is checked for overflow.
- Views keep their backing storage reachable.
- No unchecked `next()`, `prev()`, or address-based `offset()` operations exist.
- A view cannot access memory outside its backing collection.

---

## 2.4 UniquePointer<escape><T></escape>

### Purpose

`UniquePointer<T>` provides exclusive ownership of a managed value.

Its primary purpose is to express exclusive access and ownership transfer rather than to manually control allocation or freeing.

Exactly one logical owner may exist at a time.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `move()` | `UniquePointer<T>` | Transfers ownership to a new handle |
| `with_read(callback)` | Callback result | Temporarily grants read-only access |
| `with_mut(callback)` | Callback result | Temporarily grants exclusive mutable access |

Borrowing is compiler-enforced. A mutable borrow cannot be duplicated, retained, or accessed after its lifetime.

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let Point = class (
    export let x: int64 = 0;
    export let y: int64 = 0;

    export let init = func(px: int64, py: int64) {
        x = px;
        y = py;
    };
);

let.promote _ = import:Std.Core.Pointer;

let owner: UniquePointer<Point> = new(new Point(1, 2));

owner.with_mut(func(point: Point) {
    point.x = 10;
});

let transferred = owner.move();

// owner is now moved and cannot be used.

let x = transferred.with_read(
    func(point: Point) -> int64 => point.x
);
```

The callback syntax above is illustrative. The compiler must treat the received object as a scoped borrow, not an unrestricted class reference that can escape.

### Ownership rules

1. Construction transfers an unaliased value into exclusive ownership.
2. An owner cannot be copied.
3. `move()` transfers ownership and invalidates the previous owner.
4. Only one mutable borrow may exist at a time.
5. Mutable and read-only borrows cannot overlap in conflicting ways.
6. A borrowed reference cannot escape through a return value, closure, collection, global, or object field.
7. A unique owner cannot be accessed while an incompatible borrow is active.
8. Dropping the final owner releases its ownership claim; underlying storage remains subject to safe runtime memory management.

### Compiler requirements

`UniquePointer<T>` requires compiler-level ownership analysis.

The compiler must track ownership transfer across function calls, control-flow branches, returns, and closures.

The library must not simulate exclusive ownership solely through runtime reference counts.

### Safety guarantees

- No use-after-move.
- No ownership duplication.
- No dangling borrows.
- No mutation through conflicting borrows.
- No manual memory release.
- No ability to bypass ownership with a normal class-reference escape.

---

## 2.5 SharedPointer<escape><T></escape>

### Purpose

`SharedPointer<T>` allows multiple strong references to a managed object.

It is intended for data structures, graph representations, shared application objects, and other cases where multiple logical owners are appropriate.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `clone()` | `SharedPointer<T>` | Creates another strong owner |
| `read()` | `T` | Reads the referenced value under Neura's normal type rules |
| `with_read(callback)` | Callback result | Provides scoped read access |
| `with_mut(callback)` | Callback result | Provides task-local, checked mutable access |
| `downgrade()` | `WeakPointer<T>` | Creates a weak reference |

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let.promote _ = import:Std.Core.Pointer;

let Node = class (
    export let value: int64 = 0;
    export let parent: WeakPointer<Node> = nil;

    export let init = func(initial: int64) {
        value = initial;
    };
);

let root: SharedPointer<Node> = new(new Node(1));

let alias = root.clone();

let first = root.read().value; // 1
let second = alias.read().value; // 1

let weak = root.downgrade();
```

### Ownership semantics

A strong reference keeps its target reachable.

Cloning does not duplicate the underlying object.

The object remains reachable until no strong references or other runtime roots keep it alive.

Weak references do not contribute to strong reachability.

A public `ref_count()` is intentionally excluded from the initial API. Neura's garbage collection and ordinary class aliases make raw ownership counts unsuitable as correctness guarantees.

### Mutability and concurrency

`SharedPointer<T>` does not automatically make mutable `T` thread-safe.

Mutable targets remain subject to Neura's task-isolation rules.

Sharing a mutable target between tasks through `SharedPointer<T>` alone is prohibited.

When cross-task sharing is required, use an immutable target, `AtomicPointer<T>`, or `LockedPointer<T>`, depending on the operation.

### Safety guarantees

- Strong references preserve object reachability.
- No manual freeing of shared objects.
- No dangling references when ownership changes.
- Strong and weak references interact safely with garbage collection.
- No unsynchronized mutable cross-task access.
- Cyclic structures remain safe under Neura's tracing garbage collector.

---

## 2.6 WeakPointer<escape><T></escape>

### Purpose

`WeakPointer<T>` references an object without keeping it alive.

It is useful for parent references, observers, caches, and object graphs in which one relationship should not extend an object's lifetime.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `upgrade()` | `Optional<SharedPointer<T>>` | Attempts to obtain a live strong reference |
| `is_alive()` | `bool` | Reports whether the target can currently be reached |

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let.promote _ = import:Std.Core.Pointer;
let optional = import:Std.Core.Optional;

let resource: SharedPointer<Point> = new(new Point(3, 4));

let weak = resource.downgrade();

let result = weak.upgrade();

match (result) {
    optional.Optional.Some(strong) => {
        let x = strong.read().x;
    },
    optional.Optional.None => {
        // The object is no longer available.
    },
}
```

### Upgrade semantics

An upgrade must be atomic with respect to garbage collection.

If the operation succeeds, the returned strong reference keeps the object alive.

If the object has already become unavailable, the operation returns `None`.

The result never contains an invalid or partially destroyed object.

`is_alive()` is advisory. The target may become unavailable immediately after a successful check, so callers must use `upgrade()` to access it.

### Safety guarantees

- Weak references cannot produce dangling dereferences.
- No direct dereference is available.
- A successful upgrade creates a valid strong reference.
- Failed upgrades return `None`.
- Garbage collection cannot invalidate a successfully upgraded strong owner.

---

## 2.7 SoftPointer<escape><T></escape>

### Purpose

`SoftPointer<T>` provides a garbage-collector-aware reference suitable for caches.

Unlike a strong pointer, a soft reference allows the runtime to discard its target when the target is otherwise unreachable and memory management determines that retaining it is undesirable.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `fetch()` | `Optional<SharedPointer<T>>` | Attempts to obtain a strong owner |
| `is_available()` | `bool` | Advisory availability check |
| `clear()` | `void` | Releases the soft reference's retention claim |

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let.promote _ = import:Std.Core.Pointer;
let optional = import:Std.Core.Optional;

let data: int64[] = [10, 20, 30];

let strong: SharedPointer<int64[]> = new(data);
let cache: SoftPointer<int64[]> = new(strong);

let result = cache.fetch();

match (result) {
    optional.Optional.Some(hit) => {
        let first = hit.read()[0];
    },
    optional.Optional.None => {
        // Reconstruct or reload the cached data.
    },
}
```

### Garbage-collector integration

`SoftPointer<T>` must be integrated into Neura's garbage collector.

Soft references may not be implemented through untracked object addresses or independently managed memory.

The collector determines whether to retain or clear softly reachable objects.

Strong references always take precedence over soft-reference clearing.

### Safety guarantees

- No dangling references following cache eviction.
- A successful `fetch()` returns a live strong owner.
- Clearing a cache cannot invalidate unrelated strong references.
- No manual allocation or freeing.
- Cache eviction affects performance and availability, never memory correctness.

### Limitations

Soft references do not guarantee cache retention.

Programs must treat every `fetch()` as potentially returning `None`.

Memory-pressure heuristics are runtime policy, not part of the language's deterministic execution guarantees.

---

## 2.8 AtomicPointer<escape><T></escape>

### Purpose

`AtomicPointer<T>` provides atomic access to shared object references.

It supports coordinated replacement of immutable objects without exposing raw addresses or unsynchronized mutable state.

Its primary use cases include configuration updates, immutable snapshots, and concurrent state publication.

### Type restrictions

For the initial implementation, `T` must satisfy Neura's deep-immutability and transferability requirements.

Mutable classes and mutable collections cannot be stored in a cross-task `AtomicPointer<T>`.

These restrictions require compiler enforcement, including checks of generic type arguments.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `load()` | `SharedPointer<T>` | Atomically obtains a strong reference |
| `store(value)` | `void` | Atomically replaces the reference |
| `swap(value)` | `SharedPointer<T>` | Atomically replaces and returns the previous reference |
| `compare_exchange(expected, desired)` | `bool` | Replaces the reference if its identity matches |

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let.promote _ = import:Std.Core.Pointer;

#Immutable
let Config = class (
    export last debug: bool;

    export let init = func(enabled: bool) {
        debug = enabled;
    };
);

let cfg_a: SharedPointer<Config> = new(new Config(false));
let cfg_b: SharedPointer<Config> = new(new Config(true));

let atomic: AtomicPointer<Config> = new(cfg_a);

let original = atomic.load();

let previous = atomic.swap(cfg_b);

let restored = atomic.compare_exchange(cfg_b, cfg_a);

let current = atomic.load();

let debug = current.read().debug;
```

### Atomicity requirements

All reference operations must be atomic with respect to:

- Other threads or Neura tasks.
- Garbage-collector tracing.
- Object reachability.
- Reference replacement.
- Strong-reference acquisition.

`load()` must never return a reference to an object that has already become invalid.

`compare_exchange()` compares managed object identity, not raw memory addresses.

### Memory ordering

The initial implementation should provide sequentially consistent operations.

More relaxed memory-ordering options are outside the initial proposal.

An optimization must not introduce observable data races or invalid reference states.

### Safety guarantees

- No torn reference reads or writes.
- No garbage-collector race during reference acquisition.
- No dangling references following atomic replacement.
- No arbitrary address-based compare-and-swap.
- No mutation of shared objects without synchronization.
- No bypass of Neura's task-transfer restrictions.

---

## 2.9 LockedPointer<escape><T></escape>

### Purpose

`LockedPointer<T>` provides synchronized access to mutable state.

It is intended for values that must be shared safely across tasks but cannot be represented as immutable snapshots.

The implementation should reuse Neura's existing `Std.Concurrent.Guarded<T>` synchronization machinery.

### Proposed API

| Method | Return type | Behavior |
|---|---|---|
| `read()` | `T` | Reads a copy or immutable value, where safe |
| `update(callback)` | `void` | Replaces state using a synchronized transformation |
| `with(callback)` | Callback result | Executes a scoped operation under synchronization |

### Example

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let.promote _ = import:Std.Core.Pointer;

let counter: LockedPointer<int64> = new(0);

counter.update(
    func(value: int64) -> int64 => value + 1
);

counter.update(
    func(value: int64) -> int64 => value + 1
);

let result = counter.read(); // 2
```

### Synchronization rules

1. At most one mutable operation may hold the lock at a time.
2. Locks remain held for the entire protected operation.
3. All lock acquisition and release is managed by the runtime.
4. A callback cannot retain an unsynchronized mutable reference to the protected state.
5. Returned values cannot expose the protected object's mutable storage.
6. Nested access must follow a defined policy and cannot silently corrupt lock state.
7. Runtime failures must leave the lock infrastructure in a valid state.

For mutable reference-valued `T`, `read()` cannot simply return the protected object. It must be restricted to safe value types or return an independently owned immutable snapshot.

### Scoped operations

`with(callback)` allows access to protected state for the duration of the callback.

The compiler and runtime must prevent protected mutable state from escaping through callback return values, captures, or stored aliases.

This is particularly important when `T` is a mutable class.

### Safety guarantees

- No unsynchronized access to protected mutable state.
- No mutable reference escapes.
- No invalid lock or guard lifetime.
- No unsafe manual unlocking.
- No data races through the public API.

---

# 3. Shared Design Requirements

## 3.1 Memory management

All pointer targets remain under Neura's managed runtime.

The library must not expose any form of:

```text
malloc
free
realloc
reinterpret_cast
address_of
transmute
unchecked_deref
```

No pointer may be converted into an arbitrary integer address or reconstructed from one.

Memory allocation failures must result in a defined failure rather than memory corruption.

## 3.2 Nullability

Nil pointers follow Neura's existing reference semantics.

A nil pointer is not equivalent to an arbitrary or uninitialized memory address.

Calling an instance method on a nil pointer results in a defined runtime trap.

For operations where a target may no longer be available, use `Optional<T>` rather than returning a potentially invalid pointer.

This applies particularly to `WeakPointer.upgrade()` and `SoftPointer.fetch()`.

## 3.3 Lifetime and ownership

- Strong pointers keep managed objects reachable.
- Weak pointers do not keep their targets alive.
- Soft pointers permit runtime-controlled retention.
- Unique pointers use compiler-tracked ownership transfer.
- Scoped borrows cannot outlive the operation that created them.
- Collection views preserve the lifetime of their backing storage.
- Atomic reference replacement cannot invalidate an existing strong owner.

No pointer operation may rely on an untracked external allocation's lifetime.

## 3.4 Concurrency

The library must respect Neura's existing task-isolation model.

Regular mutable pointers cannot cross task boundaries merely because they are wrapped in a pointer class.

Safe cross-task mechanisms are limited to representations with enforceable synchronization and transferability rules.

`AtomicPointer<T>` and `LockedPointer<T>` must integrate with those rules rather than circumvent them.

## 3.5 Read-only access

Read-only access must be enforced at the type-system level.

A read-only pointer cannot return an unrestricted mutable alias to its protected referent.

Where the existing language cannot express the required borrow restrictions, compiler support is mandatory before the API can be released.

## 3.6 Runtime failures

Operations that fail because of invalid indices, nil references, or unavailable objects must have defined behavior.

A safe runtime trap is acceptable.

Reading arbitrary memory, corrupting an allocation, or continuing with a dangling reference is never acceptable.

---

# 4. Complete Usage Example

The following is a proposed usage example written with Neura's declaration and expression syntax. It is not an executable test until the library and its compiler-level ownership and borrowing contracts are implemented.

<!-- example: fragment: specified for 0.3.1 and not yet implemented -->
```neura
let.promote _ = import:Std.Core.Pointer;
let cell = import:Std.Core.Cell;
let optional = import:Std.Core.Optional;
let program = import:Std.Program;

// ---------- Models ----------

let Point = class (
    export let x: int64 = 0;
    export let y: int64 = 0;

    export let init = func(px: int64, py: int64) {
        x = px;
        y = py;
    };
);

let Node = class (
    export let value: int64 = 0;
    export let parent: WeakPointer<Node> = nil;
    export let children: SharedPointer<Node>[list] = [];

    export let init = func(initial: int64) {
        value = initial;
    };
);

#Immutable
let Config = class (
    export last debug: bool;

    export let init = func(enabled: bool) {
        debug = enabled;
    };
);

// ---------- Functions ----------

let double = func(value: int64) -> int64 => value * 2;

let sum = func(data: ConstPointer<int64>) -> int64 {
    return data.read();
};

// ---------- Program ----------

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {

        // ========================================
        // Pointer and ConstPointer
        // ========================================

        {
            let storage = cell.of<int64>(0);
            let p: Pointer<int64> = new(storage);

            let snapshot = p.read();

            p.write(10);

            let copy_unchanged = snapshot == 0; // true
            let storage_changed = storage.value == 10; // true

            let readonly = p.as_const();
            let value = sum(readonly); // 10

            // readonly.write(20); // Compile error

            let alias = p.clone();
            alias.write(100);

            let shared_value = p.read() == 100; // true
        }

        // ========================================
        // ListPointer
        // ========================================

        {
            let arr: int64[] = [1, 2, 3, 4, 5];

            let lp: ListPointer<int64> = new(arr, 0, arr.length);

            let middle = lp.sub(1, 4);

            let size = middle.len(); // 3
            let first = middle.value_at(0); // 2

            middle.set(1, 30);

            let updated = arr[2] == 30; // true

            // middle.value_at(3); // Bounds trap
        }

        // ========================================
        // UniquePointer
        // ========================================

        {
            let owner: UniquePointer<Point> = new(new Point(1, 2));

            owner.with_mut(func(point: Point) {
                point.x = 5;
            });

            let transferred = owner.move();

            // owner cannot be used after its move.

            let x = transferred.with_read(
                func(point: Point) -> int64 => point.x
            );

            // x == 5
        }

        // ========================================
        // SharedPointer and WeakPointer
        // ========================================

        {
            let root: SharedPointer<Node> = new(new Node(1));
            let child: SharedPointer<Node> = new(new Node(2));

            child.with_mut(func(node: Node) {
                node.parent = root.downgrade();
            });

            root.with_mut(func(node: Node) {
                node.children.append(child.clone());
            });

            let alias = root.clone();

            let parent_ref = child.with_read(
                func(node: Node) -> WeakPointer<Node> => node.parent
            );

            let upgraded = parent_ref.upgrade();

            match (upgraded) {
                optional.Optional.Some(parent) => {
                    let value = parent.with_read(
                        func(node: Node) -> int64 => node.value
                    );
                },
                optional.Optional.None => {
                    // Parent no longer exists.
                },
            }
        }

        // ========================================
        // SoftPointer
        // ========================================

        {
            let data: int64[] = [10, 20, 30];

            let strong: SharedPointer<int64[]> = new(data);
            let cache: SoftPointer<int64[]> = new(strong);

            let cached = cache.fetch();

            match (cached) {
                optional.Optional.Some(hit) => {
                    let first = hit.read()[0];
                },
                optional.Optional.None => {
                    // Rebuild cached data.
                },
            }
        }

        // ========================================
        // AtomicPointer
        // ========================================

        {
            let cfg_a: SharedPointer<Config> =
                new(new Config(false));

            let cfg_b: SharedPointer<Config> =
                new(new Config(true));

            let atomic: AtomicPointer<Config> = new(cfg_a);

            let previous = atomic.swap(cfg_b);

            let restored = atomic.compare_exchange(cfg_b, cfg_a);

            let current = atomic.load();

            let debug = current.read().debug;
        }

        // ========================================
        // LockedPointer
        // ========================================

        {
            let counter: LockedPointer<int64> = new(0);

            counter.update(
                func(value: int64) -> int64 => value + 1
            );

            counter.update(
                func(value: int64) -> int64 => value + 1
            );

            let count = counter.read(); // 2
        }

        // ========================================
        // First-class functions remain built-in
        // ========================================

        {
            let operation: func(int64) -> int64 = double;
            let result = operation(21); // 42
        }

        return program.ExitCode.Ok;
    };
);
```

---

# 5. Explicitly Excluded Capabilities

The following capabilities are intentionally excluded from `Std.Core.Pointer`:

| Capability | Reason |
|---|---|
| `RawPointer` | Unrestricted memory addressing and casting |
| `ArrayPointer` | Unchecked collection traversal |
| `NonNilPointer` | Unnecessary separate abstraction for managed storage |
| `VolatilePointer` | Volatile access is not sufficient synchronization |
| `ThreadLocalPointer` | Task-local state belongs in the concurrency library |
| `FuncPointer` | Ordinary Neura function values already provide typed callable references |
| `MemberPointer` | Can bypass accessor and field visibility guarantees |
| `PhantomPointer` | GC-triggered native cleanup complicates lifecycle guarantees |
| Explicit `free()` | Allows manual invalidation and deallocation hazards |
| Integer-to-pointer casts | Allow fabrication of arbitrary addresses |
| Pointer arithmetic | Allows unrestricted navigation outside managed object boundaries |
| Unsafe escape hatches | Undermine the library's safety contract |

Native interoperability should remain isolated behind separately specified, trusted boundaries.

It must not be introduced into this library through alternate names or helper methods.

---

# 6. Implementation and Verification Requirements

## 6.1 Compiler requirements

The compiler must enforce:

- Valid generic type arguments.
- Deep immutability where required.
- Ownership transfer for `UniquePointer<T>`.
- Use-after-move rejection.
- Scoped borrow restrictions.
- Prevention of mutable reference escapes.
- Cross-task transferability constraints.
- Restrictions on casting or converting pointer types.
- Prohibition of invalid construction paths.

Any compiler optimization must preserve these guarantees.

## 6.2 Runtime requirements

The runtime must provide:

- Stable managed references.
- Strong and weak garbage-collector integration.
- Atomic weak-reference upgrades.
- Safe soft-reference clearing.
- Atomic reference replacement.
- Checked collection access.
- Checked pointer and view metadata.
- Safe synchronization primitives.
- Defined behavior for allocation failure and invalid access.

The library must not rely on unchecked addresses or implementation-specific memory layouts visible to ordinary Neura programs.

## 6.3 Required tests

Before release, the implementation must include positive and negative tests for:

1. Strong-reference reachability.
2. Weak-reference expiration and upgrade.
3. Soft-reference clearing during garbage collection.
4. Bounds validation, including negative and overflowing indices.
5. Nested collection views.
6. Collection resizing and view invalidation.
7. Unique ownership transfers.
8. Use-after-move compile-time rejection.
9. Borrow escape compile-time rejection.
10. Conflicting mutable borrow rejection.
11. Const-correctness for nested mutable objects.
12. Concurrent atomic loads, swaps, and compare-and-exchange.
13. Garbage collection during atomic reference operations.
14. Locked state mutation under contention.
15. Attempts to leak protected mutable state.
16. Nil reference handling.
17. Generic type restrictions.
18. Compiler optimization equivalence.

Tests must verify both successful operations and attempts to violate safety requirements.

Passing ordinary functional tests alone is not sufficient for release.

---

# 7. Release Priorities

| Phase | Pointer types | Prerequisites |
|---|---|---|
| 1 | `Pointer<T>`, `ConstPointer<T>`, `ListPointer<T>` | Managed storage, immutability checks, collection bounds |
| 2 | `SharedPointer<T>`, `WeakPointer<T>` | GC integration and atomic weak upgrade |
| 3 | `UniquePointer<T>` | Compiler-enforced ownership and scoped borrowing |
| 4 | `LockedPointer<T>`, `AtomicPointer<T>` | Synchronization and transferability enforcement |
| 5 | `SoftPointer<T>` | Collector retention and clearing policy |

The phases represent implementation order, not permission to ship incomplete safety guarantees.

An abstraction must remain unavailable until its requirements are satisfied.

---

# 8. Final Design Principle

`Std.Core.Pointer` should make explicit reference management possible without exposing the unsafe operations traditionally associated with pointers.

Programmers should be able to choose between:

- Ordinary managed references.
- Read-only managed references.
- Bounds-checked collection views.
- Exclusive ownership.
- Shared ownership.
- Weak ownership.
- Garbage-collector-aware caching.
- Atomic shared references.
- Synchronized mutable state.

These choices affect performance, ownership, synchronization, and lifetime semantics, but **none may weaken Neura's memory-safety guarantees**.

A pointer's validity must never depend on the programmer correctly managing a raw memory address.

The language, compiler, and runtime are responsible for enforcing the contract.
