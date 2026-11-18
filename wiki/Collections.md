<!-- Generated from docs/lang/Collections.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Arrays and lists

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

Arrays and lists are built-in, homogeneous collections. Their type is formed by adding a suffix to the element type:

| Type | Meaning |
| --- | --- |
| `string[]` | Array of strings. |
| `string[list]` | List of strings. |
| `string[][]` | Array of arrays of strings. |
| `string[list][list]` | List of lists of strings. |
| `string[list][]` | Array of lists of strings. |

Read suffixes from left to right: each suffix wraps the type to its left. Nested collections are collections of collections, not necessarily rectangular matrices. Each inner collection has its own length; rows can have different lengths.

## Construction and length

A bracketed literal constructs an array using the expected type. Its elements must be assignable to that type's element type. An empty literal requires an expected type. A list is constructed with `new`: `new([1, 2, 3])` where the list type is known from context, `new int[list]([1, 2, 3])` with the type written, `new` alone for an empty list of a known type, and `new(0 .. count)` or `new int[list](0 .. count)` from a range. The elements, or the range's bounds, are evaluated once, in written order, straight into the list. A bare literal or range where a list is expected is the error `E0450`, and `neura check --fix` inserts the `new` and keeps the literal, its comments and its order. A nested list needs its own `new`, and a list that is already a value, such as the result of a call, needs none. For example:

<!-- example: check -->
```neura
let names: string[] = ["Ada", "Lin"];
let queue: string[list] = new(["first", "second"]);
let empty: string[list] = new;
let rows: string[][] = [["a", "b"], ["c"]];
```

An array's length is fixed when it is created, including when that length is determined at runtime. The length is a property of the value, not part of the type: arrays of different lengths can both have type `string[]`. Elements can be replaced, but an array cannot be extended, shortened, or resized. Assigning a different array to a variable replaces its value; it does not resize the original array.

### Ranges

`start .. stop` constructs a collection starting at `start` and ending before `stop`. Supply a step with `start .. stop : step`. Braces may group a range as `{start .. stop : step}`. The step defaults to one. A negative step counts down. A range whose start has already reached or passed its stop in the selected direction is empty. A zero step traps, including for an empty range.

<!-- example: check -->
```neura
let numbers = {0 .. 5};                 // int[] containing 0, 1, 2, 3, 4
let evens: int[list] = new(0 .. 8 : 2); // [0, 2, 4, 6]
let descending = 5 .. 0 : -2;          // [5, 3, 1]
```

An expected array or list type determines the result and the integer type of the bounds and step; a list is constructed with `new(start .. stop)`. Without that context the result is an array and the start determines its element type. Bounds and step are evaluated once, from left to right. Construction checks the result length against the `int32` limit and the applicable stack or heap limit and does not advance past the last element. A constructed list starts with version zero.

Binary operators bind more tightly than `..` and the conditional operator binds more loosely. Group a stepped range with parentheses or braces in a conditional's middle arm so its step colon stays separate from the conditional's colon. Ranges can be passed, returned, indexed after grouping, or used directly as a for-each source.

A list can grow or shrink through its built-in operations. Both collections expose their current element count as `length`, of type `int` (`int32`). `append(value)` adds an element to a list, and `remove_at(index)` removes the element at an index and shifts subsequent elements left. These operations are not available on arrays.

A list's read-only `version` is a `uint64`. It starts at zero and advances modulo 2^64 on every successful append or removal. Element assignment leaves it alone. Wrappers can save it when they create an iterator and compare it before advancing, including after a structural change made through another reference to the same list.

## Array lifetime

A fresh array belongs to the lexical block that creates it, including each iteration of a loop. Leaving that block releases its storage. An inner block can borrow an outer array. Assignment aliases an array, with the same cells and identity. An assignment that would keep an array or any of its backing rows past their owner is an [E0451](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0451.md) error.

A function can return a fresh array and its backing arrays by transferring their storage to the caller. Returning a parameter, global array, or a row borrowed from either is an error. The rule also applies to arrays inside tuple and enum results.

<!-- example: check -->
```neura
let grid = func(limit: int) -> int[][] {
    last shared = 0 .. limit;
    return [shared, [9], shared];
};
```

A class field, list element, message, task argument, or escaping closure cannot retain an activation array. A non-retaining call can borrow it for the call. Use a list, including a list of lists for ragged data, when the collection needs to live in heap storage. An array initialized as a global or static field lives for the execution.

The runner counts region allocations against `--max-stack-cells` and reports their peak with `--stats`. List allocation counts against the heap limit.

## Indexing and safety

Indexing is zero-based. Reading or writing `collection[index]` is valid only when the index is an integer in the range `0 <= index < collection.length`. Every access is bounds checked; an invalid index, including a negative index, causes a bounds error and terminates the current operation before any out-of-bounds memory is accessed. `remove_at` has the same index requirement. An implementation may eliminate a check only when it can prove that doing so preserves this behavior. Nested indexing checks each level independently.

Collection operations must also check length and allocation arithmetic for overflow and fail safely if a requested size cannot be represented or allocated. Safe Neura code cannot use collection indexing or list operations to read or write outside the collection's storage. This guarantee does not by itself specify the safety of external native code.

The spelling of bounds and allocation errors, error handling, list ownership, collection equality, and general allocation APIs remain to be specified. In particular, a fixed array length does not imply immutable elements or exclusive ownership.

A negative index fails with trap reason 2 (bounds failure); an index at or past the length is a bounds fault detected by NVM. Both end the run with status 139, `ExitCode.SegmentationFault` ([Errors](Errors)).

## Iteration

`for (let element in collection) { ... }` visits an array's or a list's elements in index order ([Core language](CoreLanguage#loops)). Assigning to elements by index during the loop is allowed. Appending to or removing from a list while a for-each loop visits it, through any alias, makes the loop trap with reason 5 before its next advance; each list carries a structural version that every successful `append` and `remove_at` changes.

The same loop visits any value that implements `Std.Collections.Iterable<T>`, calling `iterator()` once and then `has_next()` and `next()` ([Core language](CoreLanguage#loops)). Every standard-library collection, `Sequence`, and `Std.Core.IntRange` implement it, and a program's own classes can too. A source that is not an array, list, or `Iterable<T>` is rejected, as is an element annotation that differs from `T`.

## FrozenList

`Std.Collections.Frozen.FrozenList<T>` is a list that nobody can change, with storage nobody else holds. Its element type must be deeply immutable: `bool`, a number, `string`, a tuple or enum of such parts, an `#Immutable` class whose fields are too, or a `FrozenList` of them (`E0580` at the type argument otherwise, also when a generic function passes its type parameter on). The class is `#Immutable`, so a [UI state](StateAndComponents) may hold one. It cannot yet cross to another task: the image format admits no collection in a transferable layout, so sending a `FrozenList` on a channel or to `spawn` is `E0602` in 0.3.0.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let frozen = import:Std.Collections.Frozen;
let program = import:Std.Program;

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let source: string[] = ["bolt", "nut"];
        let parts = frozen.copy_of<string>(source);
        source[0] = "gear";
        let more = parts.appended("washer");
        console.print_line(parts.to_string() + " " + more.to_string());
        for (let part: string in more) {
            console.print_line(part);
        }
        return program.ExitCode.Ok;
    };
);
```

```text
[bolt, nut] [bolt, nut, washer]
bolt
nut
washer
```

| Member | Meaning |
| --- | --- |
| `frozen.copy_of<T>(array)`, `frozen.copy_of_list<T>(list)`, `frozen.of<T>(items...)`, `frozen.empty<T>()` | Copy the elements into new private storage. Nothing freezes a list in place, so a later change to the source is not seen. |
| `length()`, `is_empty()`, `get(index)`, `first_or_none()`, `last_or_none()`, `contains(value)`, `index_of(value)`, `for_each(action)`, for-each | Reads. `get` with an index out of range traps as a list index does. `contains` and `index_of` compare with `===`. |
| `slice(start, end)`, `appended(value)`, `replaced(index, value)`, `removed(index)`, `concat(other)` | Return a new list and leave this one unchanged. They copy; no storage is shared between lists. |
| `to_list()` | A fresh built-in list; changing it changes nothing in the `FrozenList`. |
| `equals`, `hash_code`, `to_string` | Compare elements by value (`===`) at each index, fold the element hashes as a list does, and print `[a, b]`. `==` stays reference identity. |

The storage is a `last` list field tagged [`#Frozen`](Tags#frozen), which only the standard library may write. The compiler checks that the constructor assigns it once, from a list the constructor created and only filled, and that the class reads it only by index and length (`E0581`); with that, the `#Immutable` check accepts the field. `ImmutableList`, `ImmutableSet` and `ImmutableMap` keep their meaning, read-only views of a shallow copy: they are not deeply immutable and are rejected where a deeply immutable type is required.

See [collections.nra](https://github.com/Kayyo321/neura/blob/main/examples/types/collections.nra) for examples of indexing, mutation, and nested types; the test driver in `tests/backend/examples/collections-driver.nra` builds and runs it.
