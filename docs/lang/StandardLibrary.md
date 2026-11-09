# Standard library and I/O

`SysIo` and `SysIo.Entry` appear in existing examples. Their exact signatures, encoding, error behavior and platform mapping are not yet specified. The following are initial library requirements, not implemented APIs:

| Area | Initial capability | Decisions required |
| --- | --- | --- |
| Console | Read a line; write text and lines | EOF, encoding, fallible writes |
| Text | UTF-8 text, concatenation and length | Indexing units, normalization, invalid input |
| Collections | Arrays and lists, length, indexing and list mutation | Aliasing, allocation and equality |
| Files and paths | Open, read, write and close; path composition | Error results, encoding, platform semantics |
| Conversion | Parse and format numbers | Overflow, locale, invalid input |
| Testing | Assertions and runner integration | See [Testing](Testing.md) |

The initial file example in [errors.nra](../../examples/guide/errors.nra) is intentionally pseudocode until failure and resource APIs are settled. No network, filesystem, or numeric conversion API should be assumed from names alone. Standard modules need versioned contracts and examples for success and failure before implementation.
