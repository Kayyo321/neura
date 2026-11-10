# `Std.Text.Regex`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Text.Regex`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Text.Regex`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Regex** · [`export let RegexFlags = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L31)
  Stores three matching options whose values participate in equality and hashing.
- **Regex.RegexFlags** · [`export let ignore_ascii_case: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L33)
  Enables ASCII-only case-insensitive matching.
- **Regex.RegexFlags** · [`export let multi_line: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L35)
  Makes caret and dollar match LF-delimited line boundaries.
- **Regex.RegexFlags** · [`export let dot_all: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L37)
  Allows dot to match LF.
- **Regex.RegexFlags** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L40)
  Compares flags by their three Boolean values.
- **Regex.RegexFlags** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L50)
  Encodes the enabled flags as a bit mask.
- **Regex.RegexFlags** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L54)
  Formats the flags as a RegexFlags description.
- **Regex** · [`export let RegexMatch = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L60)
  One match: group 0 is the whole match. Immutable.
- **Regex.RegexMatch** · [`export let init = func(text: string, captured: int[list], group_names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L69)
  Stores the original input, capture offsets, and capture names.
- **Regex.RegexMatch** · [`export let start = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L76)
  Returns the inclusive byte offset of the whole match.
- **Regex.RegexMatch** · [`export let end = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L79)
  Returns the exclusive byte offset of the whole match.
- **Regex.RegexMatch** · [`export let text = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L82)
  Returns the whole matched substring.
- **Regex.RegexMatch** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L85)
  Tests equality of input, capture offsets, and group names.
- **Regex.RegexMatch** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L94)
  Combines the hashes of the input, capture offsets, and group names.
- **Regex.RegexMatch** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L98)
  Returns the whole matched substring.
- **Regex.RegexMatch** · [`export let group = func(index: int) -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L102)
  The group's text, or None when the group did not participate. An index outside 0 through group_count() traps with "invalid argument: no such group".
- **Regex.RegexMatch** · [`export let group_start = func(index: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L112)
  The group's start offset, or -1 when it did not participate.
- **Regex.RegexMatch** · [`export let named_group = func(name: string) -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L119)
  The named group's text, or None when it did not participate. A name the pattern does not define traps with "invalid argument: no such group".
- **Regex** · [`export let Regex = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L131)
  Stores a compiled pattern and provides matching, replacement, and splitting.
- **Regex.Regex** · [`export let init = func(text: string, compiled: machine.Program, group_names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L140)
  Called by compile_with; the program type is internal to the library.
- **Regex.Regex** · [`export let is_match = func(input: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L147)
  Tests whether the pattern matches anywhere in the input.
- **Regex.Regex** · [`export let matches_fully = func(input: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L151)
  Whether the pattern matches the whole input.
- **Regex.Regex** · [`export let find = func(input: string) -> RegexMatch`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L155)
  Returns the first match or nil when none exists.
- **Regex.Regex** · [`export let find_from = func(input: string, offset: int) -> RegexMatch`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L162)
  The first match starting at or after the offset, or nil. The offset must be a character boundary from 0 through the byte length; otherwise the call traps with "invalid argument: offset out of range" or "invalid argument: offset is not a character boundary". ^ and \b still see the text before the offset.
- **Regex.Regex** · [`export let find_all = func(input: string) -> RegexMatch[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L169)
  Successive non-overlapping matches. After an empty match the search resumes one scalar value later.
- **Regex.Regex** · [`export let replace_all = func(input: string, replacement: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L196)
  Replaces every match of find_all. In the replacement, $0 through $99 (two digits taken when present) and ${name} or ${number} insert a group, and $$ is a dollar sign; a group that did not participate inserts nothing. A reference to a group the pattern does not have traps with "invalid argument: no such group"; any other $ traps with "invalid argument: malformed replacement". The replacement is checked before matching.
- **Regex.Regex** · [`export let replace_first = func(input: string, replacement: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L200)
  Replaces the first match, with the rules of replace_all.
- **Regex.Regex** · [`export let split = func(input: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L211)
  The pieces between the matches of find_all, with the rules of Strings.split: empty pieces are kept, and an input without a match gives one piece.
- **Regex.Regex** · [`export let group_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L223)
  The number of capturing groups, not counting group 0.
- **Regex.Regex** · [`export let pattern = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L226)
  Returns the original pattern text.
- **Regex.Regex** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L229)
  Tests equality of the original pattern and recorded compilation options.
- **Regex.Regex** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L238)
  Hashes the original pattern and recorded compilation options.
- **Regex.Regex** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L242)
  Returns the original pattern text.
- **Regex** · [`export let compile = func(pattern: string) -> result.Result<Regex>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L287)
  Compiles the pattern with every flag off.
- **Regex** · [`export let compile_with = func(pattern: string, flags: RegexFlags) -> result.Result<Regex>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/regex.nra#L292)
  Compiles the pattern; a syntax error or an exceeded limit is a Parse error at the offending pattern offset. The flags are read once.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.7 Conformance summary

These are the intended source-module contracts. Compiler support for the operations does not imply that a collection module or adapter is already packaged.

| Type | Equality and hash | Natural order | Iterable | Interfaces (G4) |
| --- | --- | --- | --- | --- |
| Integers, floats, `bool`, fieldless enums, `string`, tuples | Built in (G3a) | Companion `compare` functions | No (`Strings.scalars` gives a list) | |
| Array, list | Built in, structural (G3a) | No | Built-in for-each | |
| `Optional<T>`, `Result<T>`, `Error`, `JsonValue` | Module functions; payload enums have no built-in equality | No | No | |
| `ArrayList`, `LinkedList` | As lists | No | Yes | `MutableList`; `LinkedList` also `Deque` |
| `ArrayDeque`, `PriorityQueue` | Identity | No | Yes | `Deque` or `Queue` |
| `HashSet`, `TreeSet`, `BitSet` | As sets | No | Yes | `MutableSet`; `TreeSet` also `SortedSet` |
| `HashMap`, `TreeMap`, `OrdinalMap` | As maps | No | Yes, of `[K, V]` | `MutableMap`; `TreeMap` also `SortedMap` |
| `BigInteger`, `BigDecimal`, `Duration`, `Instant`, the calendar types, `Path`, `Uuid` | Yes, by value | `compare_to` member | No | `Comparable` |
| `RegexFlags`, `RegexMatch`, `Regex` | By value | No | No | |
| `StringBuilder`, `ByteBuffer` | Current byte sequence | No | No | |
| `Sequence`, `Random`, `Logger`, stream adapters, files, sockets | Identity | No | `Sequence` once | Streams: [section 11.1](Stdlib-Design#111-stdio) |

### 8.4 `Regex`

A class holding a compiled regular expression. Needs T1 only: its types are all concrete. Matching runs in time proportional to the pattern size times the input length and never backtracks.

**Syntax.** Literals; `.`; character classes with ranges and negation; `\d`, `\w`, `\s` and their negations (ASCII definitions); `\t`, `\n`, `\r`, `\\`, escaped metacharacters, and `\u{...}`; anchors `^` and `$`; `\b` and `\B`; grouping, non-capturing groups, and named groups; alternation; the quantifiers `*`, `+`, `?`, `{n}`, `{n,}`, `{n,m}`, each with a lazy form. **Not supported:** backreferences, lookahead, lookbehind, possessive quantifiers, and Unicode property classes; a pattern that uses them fails to compile.

**Compilation.** The module functions `compile(pattern) → Result<Regex>` and `compile_with(pattern, flags: RegexFlags) → Result<Regex>`. `RegexFlags` is a class with three `bool` fields: `ignore_ascii_case`, `multi_line`, `dot_all`. A recursive-descent parser builds a syntax tree, which is a concrete recursive payload enum (literal, class, concatenation, alternation, repetition, group, assertion). A compiler turns it into a program whose instructions are another payload enum: scalar, any, class, split (two targets in priority order), jump, save, assert, and match. Limits: a repetition count of at most 1,000, at most 10,000 instructions, and group nesting of at most 250. Exceeding a limit, or a syntax error, returns a `Parse` error with the pattern offset.

**Matching.** A Pike machine: it advances a list of threads over the input one scalar value at a time, in priority order, with at most one thread per instruction per position. Each thread carries its capture positions. The result is the leftmost match, and among matches at that start the one a backtracking matcher would find first. It reports byte offsets.

| Member | Behavior |
| --- | --- |
| `is_match(input) → bool`, `matches_fully(input) → bool` | |
| `find(input) → RegexMatch`, `find_from(input, offset) → RegexMatch` | The first match at or after the offset, or `nil` when none exists. A successful match is always a real object, including an empty match. |
| `find_all(input)` | A built-in list of `RegexMatch`: successive non-overlapping matches. After an empty match the search resumes one scalar value later. |
| `replace_all(input, replacement) → string`, `replace_first(...)` | `$0` through `$99` and `${name}` insert groups; `$$` is a dollar sign. A reference to a group that does not exist traps; a group that did not participate inserts nothing. |
| `split(input)` | A built-in list of strings, with the rules of `Strings.split`. |
| `group_count() → int`, `pattern() → string` | |

`RegexMatch` is an immutable class: `start()`, `end()`, `text()`, `group(index) → Optional<string>`, `group_start(index) → int` (-1 when the group did not participate), and `named_group(name) → Optional<string>`. Group 0 is the whole match.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.

### 18.4 Text

- **Offsets.** Library offset checks trap with `invalid argument: offset out of range` or `invalid argument: offset is not a character boundary`; `byte_at`, `slice`, and `slice_from` use the language's own traps.
- **Strings.** `lines("")` is `[""]`. `last_index_of(s, "")` is the byte length. `pad_start` and `pad_end` return the text unchanged for a width at or below its scalar count. `from_bytes` reports the offset of the first ill-formed sequence with the message `invalid start byte`, `invalid continuation byte`, or `unexpected end of data`.
- **Scalars.** `utf8_length` traps for a value that is not a scalar value. `is_ascii_whitespace` includes vertical tab, matching `trim` and `\s`.
- **Regex syntax.** `(?<name>…)` and `(?P<name>…)` both name a group. Any ASCII punctuation may be escaped; other escapes are errors. A literal `{` must be escaped. A quantified assertion is `nothing to repeat`, a second quantifier `repeated quantifier`, and `+` after a quantifier `possessive quantifiers are not supported`.
- **Regex matching.** `$` without `multi_line` matches only at the end, and `.` excludes only LF. An iteration beyond a quantifier's minimum that matches empty fails, and a capture inside a repeated group keeps its last value. The Pike machine keeps one thread per instruction per position, so a loop whose body can match empty can merge iterations and differ from every backtracking order; for example `(?:a*?)*` on `aa` matches `[0, 1)`. Replacements validate before matching, accept `${number}`, and trap on a malformed `$`. `group`, `group_start`, and `named_group` trap for an unknown index or name.
