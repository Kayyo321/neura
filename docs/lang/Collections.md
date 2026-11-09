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

A list can grow or shrink through its built-in operations. Both collections expose their current element count as `length`. `append(value)` adds an element to a list, and `remove_at(index)` removes the element at an index and shifts subsequent elements left. These operations are not available on arrays.

## Indexing and safety

Indexing is zero-based. Reading or writing `collection[index]` is valid only when the index is an integer in the range `0 <= index < collection.length`. Every access is bounds checked; an invalid index, including a negative index, causes a bounds error and terminates the current operation before any out-of-bounds memory is accessed. `remove_at` has the same index requirement. An implementation may eliminate a check only when it can prove that doing so preserves this behavior. Nested indexing checks each level independently.

Collection operations must also check length and allocation arithmetic for overflow and fail safely if a requested size cannot be represented or allocated. Safe Neura code cannot use collection indexing or list operations to read or write outside the collection's storage. This guarantee does not by itself specify the safety of external native code or future unsafe features.

The spelling of bounds and allocation errors, error handling, ownership and aliasing, collection equality, and general allocation APIs remain to be specified. In particular, a fixed array length does not imply immutable elements or exclusive ownership.

See [collections.nra](../../examples/types/collections.nra) for examples of indexing, mutation, and nested types. These examples describe the intended language design; this repository does not yet establish an implementation of these operations.
