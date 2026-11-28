# `Std.Core.Pointer`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

This module provides mutable and read-only views of managed cell storage and bounds-checked views of arrays and lists.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Pointer** · [`export let Pointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L35)
  A mutable view of a cell's retained storage. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.1.
- **Pointer.Pointer** · [`export let init = func(storage: cell.Cell<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L39)
  Retains the storage of the supplied live cell.
- **Pointer.Pointer** · [`export let read = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L45)
  Reads the current stored value.
- **Pointer.Pointer** · [`export let write = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L48)
  Replaces the current stored value.
- **Pointer.Pointer** · [`export let as_const = func -> ConstPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L53)
  Shares access to a deeply immutable stored value.
- **Pointer.Pointer** · [`export let clone = func -> Pointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L56)
  Returns another mutable view of the same storage.
- **Pointer** · [`export let ConstPointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L62)
  A read-only view that keeps a cell's storage live. A Pointer to the same storage can still change it, so the view stays in the task that holds it. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.1.
- **Pointer.ConstPointer** · [`export let read = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L71)
  Reads the stored value. T must be deeply immutable.
- **Pointer.ConstPointer** · [`export let clone = func -> ConstPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L74)
  Returns another read-only view of the same storage.
- **Pointer** · [`export let ListPointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L79)
  A non-copying, bounds-checked view of a contiguous array or list region. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.1.
- **Pointer.ListPointer** · [`export let init = func(values: T[], start: int, end: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L86)
  Creates a view of [start, end) in a fixed-length array.
- **Pointer.ListPointer** · [`export let len = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L122)
  Returns the number of elements in the view.
- **Pointer.ListPointer** · [`export let value_at = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L128)
  Reads the element at a checked index relative to this view.
- **Pointer.ListPointer** · [`export let set = func(index: int, value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L137)
  Replaces the element at a checked index without changing the list version.
- **Pointer.ListPointer** · [`export let sub = func(start: int, end: int) -> ListPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L150)
  Creates a checked subview of [start, end) relative to this view.
- **Pointer.ListPointer** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L160)
  Reports whether the view contains no elements.
- **Pointer** · [`export let UniquePointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L166)
  Exclusive ownership of a class instance (Neura0.3.1.md section 2.4). Declared with its target and task-transfer rules; it has no public constructor until its operations land. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.1.
- **Pointer** · [`export let SharedPointer = class<T>:ManagedPointer implements closeable.Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L202)
  Shared ownership of a managed object (section 2.5). Each SharedPointer is one owner claim of a group; `clone`, and `upgrade` on a WeakPointer, add claims, and `release` ends one. When the last claim ends, the group expires: every WeakPointer of it returns None. Tasks may share a SharedPointer whose target type is deeply immutable. Tagged `#Final`. Since 0.3.1.
- **Pointer.SharedPointer** · [`export let init = func(target: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L208)
  Takes ownership of `target`, which nothing else may refer to: a new object, an array or list literal, or a local that holds one and is used only here (E0465).
- **Pointer.SharedPointer** · [`export let clone = func -> SharedPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L217)
  Returns another owner claim of the same target.
- **Pointer.SharedPointer** · [`export let read = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L226)
  Returns the target under Neura's ordinary reference rules.
- **Pointer.SharedPointer** · [`export let with_read = func<U>(body: func(T) -> U) -> U`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L241)
  Runs `body` with the target and returns its result. Traps while a with_mut of the same target runs.
- **Pointer.SharedPointer** · [`export let with_mut = func(body: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L254)
  Runs `body` with the target, marking the target as written until it returns. A with_mut or with_read of the same target inside it traps. A body that needs a result writes it into the target, or uses with_read.
- **Pointer.SharedPointer** · [`export let downgrade = func -> WeakPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L267)
  Returns a WeakPointer to the target, which adds no owner claim.
- **Pointer.SharedPointer** · [`export let release = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L276)
  Ends this owner claim. Every alias of this SharedPointer is released with it. When no claim remains, the target expires. A second release traps.
- **Pointer.SharedPointer** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L288)
  Releases this owner claim if it is held, for `using`. Closing again does nothing.
- **Pointer.SharedPointer** · [`export let group_of = func(claim: Claim) -> SharedGroup<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L301)
  The group of a claim that is still held, for an AtomicPointer of this module. Only this module can name its argument.
- **Pointer.SharedPointer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L309)
  Names the pointer; the target is not shown.
- **Pointer** · [`export let WeakPointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L316)
  A non-owning reference to a shared object (section 2.6). It does not extend the target's owner claims: once the last SharedPointer of the target is released, `upgrade` returns None. Before that, the target stays allocated. Tagged `#Final`. Since 0.3.1.
- **Pointer.WeakPointer** · [`export let init = func(shared: SharedGroup<T>, claim: Claim)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L320)
  A weak reference to a group. Only SharedPointer.downgrade can name its arguments.
- **Pointer.WeakPointer** · [`export let upgrade = func -> optional.Optional<SharedPointer<T>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L326)
  Returns a new owner claim of the target, or None when the target has expired.
- **Pointer.WeakPointer** · [`export let is_alive = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L338)
  Reports whether the target is still held by an owner claim.
- **Pointer.WeakPointer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L344)
  Names the pointer.
- **Pointer** · [`export let SoftPointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L350)
  A cache reference to a shared object (section 2.7). It has no public constructor until its operations land. Tagged `#Final`. Since 0.3.1.
- **Pointer** · [`export let AtomicPointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L361)
  An atomically replaced reference to a deeply immutable object (section 2.8). The slot holds one owner claim of its current target. Every operation changes the slot and the owner counts in straight-line field operations after its last call, so no task switch splits it, and the operations on one AtomicPointer are totally ordered. Tagged `#Final`. Since 0.3.1.
- **Pointer.AtomicPointer** · [`export let init = func(initial: SharedPointer<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L365)
  An AtomicPointer whose slot holds a new owner claim of `initial`'s target.
- **Pointer.AtomicPointer** · [`export let load = func -> SharedPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L379)
  Returns a new owner claim of the current target.
- **Pointer.AtomicPointer** · [`export let store = func(value: SharedPointer<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L386)
  Replaces the target with `value`'s target. The slot's claim of the old target ends.
- **Pointer.AtomicPointer** · [`export let swap = func(value: SharedPointer<T>) -> SharedPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L404)
  Replaces the target with `value`'s target and returns the slot's claim of the old one.
- **Pointer.AtomicPointer** · [`export let compare_exchange = func(expected: SharedPointer<T>, desired: SharedPointer<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L421)
  Replaces the target with `desired`'s target if the current target is `expected`'s, comparing targets by identity, and reports whether it did. Tagged `#NoDiscard`.
- **Pointer.AtomicPointer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L444)
  Names the pointer.
- **Pointer** · [`export let LockedPointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L461)
  State that tasks reach one at a time (section 2.9), through a Std.Concurrent guard: an operation of another task waits until the current one returns, first come, first served, and an operation of the same LockedPointer inside one traps with `invalid state: guard already held`. A trap ends the run, so the guard never needs repair. Tagged `#Final`. Since 0.3.1.
- **Pointer.LockedPointer** · [`export let init = func(initial: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L465)
  A LockedPointer of a transferable value (E0602 otherwise; use `locked`).
- **Pointer.LockedPointer** · [`export let read = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L474)
  Returns the value. T must be deeply immutable (E0580).
- **Pointer.LockedPointer** · [`export let update = func(body: func(T) -> T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L477)
  Replaces the value with what `body` returns for it, under the lock.
- **Pointer.LockedPointer** · [`export let with = func<U>(body: func(T) -> U) -> U`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L485)
  Runs `body` with the value under the lock and returns its result, which must be transferable.
- **Pointer.LockedPointer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L489)
  Names the pointer; the value is not shown, since reading it needs the lock.
- **Pointer** · [`export let locked = func<T>(initializer: func() -> T) -> LockedPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L495)
  A LockedPointer of the value that `initializer` builds, for a value of any type. The initializer's captures are transferable and it is task-safe, so the value is reachable only through the lock. Since 0.3.1.
<!-- END SOURCE API -->

## Detailed contracts

A pointer keeps its storage reachable. Destroying the original cell invalidates its cell handles and leaves the pointer's storage live. A list view becomes stale when its backing list changes length. The [0.3.1 pointer contract](https://github.com/Kayyo321/neura/blob/main/docs/lang/Neura0.3.1.md#stdcorepointer) specifies construction, bounds, type restrictions, task use, and foreign interoperability.
