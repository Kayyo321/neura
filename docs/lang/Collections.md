# Arrays and lists

Arrays and lists are built-in, homogeneous collections. Their type is formed by adding a suffix to the element type:

| Type | Meaning |
| --- | --- |
| `string[]` | Array of strings. |
| `string[list]` | List of strings. |
| `string[][]` | Array of arrays of strings. |
| `string[list][list]` | List of lists of strings. |
| `string[][list]` | List of arrays of strings. |
| `string[list][]` | Array of lists of strings. |

Read suffixes from left to right: each suffix wraps the type to its left. Nested collections are collections of collections, not necessarily rectangular matrices. Each inner collection has its own length; rows can have different lengths.

## Construction and length

A bracketed literal constructs a collection using the expected type. Its elements must be assignable to that type's element type. An empty literal requires an expected type. For example:

```neura
let names: string[] = ["Ada", "Lin"];
let queue: string[list] = ["first", "second"];
let empty: string[list] = [];
let rows: string[][] = [["a", "b"], ["c"]];
```

An array's length is fixed when it is created, including when that length is determined at runtime. The length is a property of the value, not part of the type: arrays of different lengths can both have type `string[]`. Elements can be replaced, but an array cannot be extended, shortened, or resized. Assigning a different array to a variable replaces its value; it does not resize the original array.

### Ranges

`start .. stop` constructs a collection starting at `start` and ending before `stop`. Supply a step with `start .. stop : step`. Braces may group a range as `{start .. stop : step}`. The step defaults to one. A negative step counts down. A range whose start has already reached or passed its stop in the selected direction is empty. A zero step traps, including for an empty range.

```neura
let numbers = {0 .. 5};                 // int[] containing 0, 1, 2, 3, 4
let evens: int[list] = 0 .. 8 : 2;      // [0, 2, 4, 6]
let descending = 5 .. 0 : -2;          // [5, 3, 1]
```

An expected array or list type determines the result and the integer type of the bounds and step. Without that context the result is an array and the start determines its element type. Bounds and step are evaluated once, from left to right. Construction checks the result length against the `int32` and heap limits and does not advance past the last element. A constructed list starts with version zero.

Binary operators bind more tightly than `..` and the conditional operator binds more loosely. Group a stepped range with parentheses or braces in a conditional's middle arm so its step colon stays separate from the conditional's colon. Ranges can be passed, returned, indexed after grouping, or used directly as a for-each source.

A list can grow or shrink through its built-in operations. Both collections expose their current element count as `length`, of type `int` (`int32`). `append(value)` adds an element to a list, and `remove_at(index)` removes the element at an index and shifts subsequent elements left. These operations are not available on arrays.

A list's read-only `version` is a `uint64`. It starts at zero and advances modulo 2^64 on every successful append or removal. Element assignment leaves it alone. Wrappers can save it when they create an iterator and compare it before advancing, including after a structural change made through another reference to the same list.

## Indexing and safety

Indexing is zero-based. Reading or writing `collection[index]` is valid only when the index is an integer in the range `0 <= index < collection.length`. Every access is bounds checked; an invalid index, including a negative index, causes a bounds error and terminates the current operation before any out-of-bounds memory is accessed. `remove_at` has the same index requirement. An implementation may eliminate a check only when it can prove that doing so preserves this behavior. Nested indexing checks each level independently.

Collection operations must also check length and allocation arithmetic for overflow and fail safely if a requested size cannot be represented or allocated. Safe Neura code cannot use collection indexing or list operations to read or write outside the collection's storage. This guarantee does not by itself specify the safety of external native code or future unsafe features.

The spelling of bounds and allocation errors, error handling, ownership and aliasing, collection equality, and general allocation APIs remain to be specified. In particular, a fixed array length does not imply immutable elements or exclusive ownership.

A negative index fails with trap reason 2 (bounds failure); an index at or past the length is a bounds fault detected by NVM. Both end the run with status 70.

## Iteration

`for (let element in collection) { ... }` visits an array's or a list's elements in index order ([Core language](CoreLanguage.md#loops)). Assigning to elements by index during the loop is allowed. Appending to or removing from a list while a for-each loop visits it, through any alias, makes the loop trap with reason 5 before its next advance; each list carries a structural version that every successful `append` and `remove_at` changes.

See [collections.nra](../../examples/types/collections.nra) for examples of indexing, mutation, and nested types; the test driver in `tests/backend/examples/collections-driver.nra` builds and runs it.
