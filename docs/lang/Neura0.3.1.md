# Neura 0.3.1 Specification

This update will take all stubs left in 0.3.0, including unfinished Net standard libraries/host functionality and implement it. What 0.3.1 will finally do is verify end to end that Neura fufills the following: 
### "Every valid, safe Neura program must be unable to access memory outside a live, authorized allocation, regardless of its inputs or execution schedule."

and then we can genuinely say that Neura is a memory safe programming langauge.

This will also add langauge features, and only one new Std library, 
> Std.Core.Pointer

## Addition 1; [COME UP WITH GOOD NAME HERE]

[TURN THE FOLLOWING INTO MARKDOWN]
// allow the following syntax for making large numbers
// A) more easy to read with apostrphoes splicing them up and
// B) can be easily tied to one specific type:
// - u64 -> the number writen will be of type uint64
// - s32 -> the number writen will be of type int64 (signed)
// - f64 -> the number writen will be of type float64 (so on and so forth)
let x = 120'000'000u64;
let y = 64'000s32;
let z = 100s16;
let w = 13.885f64;

## Addition 2; REFACTOR:[variable initialization inside of if statements]

[TURN THE FOLLOWING INTO MARKDOWN]
if (let [valid, text] = string.from_utf8(bytes); valid) {
    console.println(text);
}

## Addition 3; [COME UP WITH GOOD NAME HERE]

[TURN THE FOLLOWING INTO MARKDOWN]
let val: Error = nil;
if (!val) {
    // when non boolean/comparision is evaluated (and it's a value that *can* be nil), evaluate if it's nil or not.
    // this body should not be ran
}

if (val) {
    console.println("this one shall!");
}

## Addition 4; REFACTOR:[the pointer standard library]

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