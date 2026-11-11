# Errors, traps, and compiler diagnostics

Neura separates **expected failures**, which are ordinary return values (`Result<T>`, `Optional<T>`, or your own enum), from **runtime failures** (traps), which end the run immediately. There are no exceptions, no `try`/`catch`, and nothing unwinds. Specification: [Errors](https://github.com/Kayyo321/neura/blob/main/docs/lang/Errors.md); diagnostics: [Tooling, Diagnostics](https://github.com/Kayyo321/neura/blob/main/docs/lang/Tooling.md#diagnostics).

## Contents

- [Designing for failure](#designing-for-failure)
- [Traps and exit statuses](#traps-and-exit-statuses)
- [Reading compiler diagnostics](#reading-compiler-diagnostics)
- [Diagnostics for agents: `--error-format`](#diagnostics-for-agents---error-format)
- [`neura explain` and `neura check --fix`](#neura-explain-and-neura-check---fix)
- [Warnings](#warnings)
- [Common diagnostics and their fixes](#common-diagnostics-and-their-fixes)

## Designing for failure

<!-- example: check -->
```neura
let error = import:Std.Core.Error;
let result = import:Std.Core.Result;
let failure = import:Std.Core.Failure;

export let Validation = enum (Ok, Empty, TooLong { limit: int });

let validate = func(words: string[], limit: int) -> Validation {
    if (words.length == 0) {
        return Validation.Empty;
    }
    if (words.length > limit) {
        return Validation.TooLong { limit: limit };
    }
    return Validation.Ok;
};

#NoDiscard
let checked_ratio = func(numerator: int, denominator: int) -> result.Result<int> {
    if (denominator == 0) {
        return result.Result<int>.Err(error.custom("math.zero", "division by zero"));
    }
    return result.Result<int>.Ok(numerator / denominator);
};

let must_be_positive = func(value: int) {
    if (value <= 0) {
        failure.trap("value must be positive");   // a deliberate trap: status 134
    }
};
```

- Return `Result<T>` when a caller can recover; return your own enum when callers branch on several outcomes; trap (with `Std.Core.Failure.trap(message)` or `trap_with_message(message)`) only for violated preconditions, which are bugs.
- Mark a function whose `Result` must not be ignored `#NoDiscard`; a caller that drops it gets warning `W0003`.
- Propagate by matching and returning the `Err`, wrapping it with `error.context(cause, "while doing X")`.
- A trap cannot be caught, including in tests. Test trapping behavior with a separate program and its exit status.

## Traps and exit statuses

A trap prints `<image>: trap: <reason> in function F block B at file.nra:line:col` (with a source map) and exits with the status of its class:

| Cause | Example message | Status |
| --- | --- | --- |
| Integer overflow, division by zero, `MIN / -1`, out-of-range `as` | `source arithmetic failure (trap reason 1)` | 136 |
| Index at or past the length | `index out of bounds: index 3 for length 1` | 139 |
| Negative index | `source bounds failure (trap reason 2)` | 139 |
| `nil` receiver | `nil class reference` | 139 |
| Field read before any store, global read before initialization | `read of an uninitialized field` | 139 |
| Recursion past `--max-frames` | | 139 |
| `Failure.trap`, `trap_with_message`, a failed `Std.Test` assertion, a failed class cast, a setter that rejects a value | `explicit program failure (trap reason 3): <message>` | 134 |
| List changed during for-each | `collection modified during iteration (trap reason 5)` | 134 |
| Deadlock (every task parked) | `deadlock: all tasks are parked` | 134 |
| A synchronized method re-entered through another reference | `invalid state: guard already held` | 134 |
| Heap, handle, or task limits | | 137 |
| `--max-steps` exceeded (default 100,000,000) | `step limit exceeded: 100000000 steps` | 152 |
| Invalid dispatch or malformed image | | 132 |

Load, bind, and usage errors from `neura run` itself exit with 1. Kernel calls and execution tiers do not change any of these: a kernel traps with the same message as the Neura code it replaced, and both tiers trap at the same step.

## Reading compiler diagnostics

Every error has a code, `E` and four digits; every warning a code, `W` and four digits, and a name. The hundreds digit names the area:

| Range | Area | Range | Area |
| --- | --- | --- | --- |
| `E01xx` | lexer: characters and literals | `E06xx` | concurrency: tasks, isolation, monitors |
| `E02xx` | parser: grammar, `;`, delimiters | `E07xx` | modules and imports, kernels |
| `E03xx` | names: unknown, duplicate, private | `E08xx` | backend and entry point |
| `E04xx` | types and bindings, `last`, `new` | `E09xx` | tags |
| `E05xx` | classes, accessors, anonymous classes | `W0001`–`W0005` | warnings |

The default `human` form shows the code, the message, the source line with the error marked `^`, related places marked `-` with a label, notes, and a `help:` with the edited line when a fix is known:

```text
error[E0201]: expected ';'
 --> app.nra:1:15
  |
1 | let limit = 10
  |               ^ expected ';' here
2 | let half = limit / 2;
  | --- found 'let'
  |
help: add the missing ';'
  |
1 | let limit = 10;
  |               +

For more information about this error, try `neura explain E0201`.
```

The lexer and the parser report several errors per run (up to `--max-errors N`, default 20). Name resolution, type checking, and the later checks **stop at the first error**, so fix the first error and check again: later errors only appear once it is gone.

## Diagnostics for agents: `--error-format`

| Form | Use it for |
| --- | --- |
| `--error-format=human` (default) | Reading; it may change between releases. |
| `--error-format=short` | One stable line per diagnostic: `file:line:column: error[E0201]: message`, or `warning[W0003]` for a warning. Match on this in scripts and tests. |
| `--error-format=json` | One JSON object per line, for a tool that applies fixes. |

A JSON object has `code` (string or `null`), `severity` (`error` or `warning`), `message`, `spans`, `notes`, `help`, `suggestions`, `args`, and `rendered` (the human form without color). Read:

- `spans[]`: `file`, `line`, `column` (1-based, in Unicode code points), `offset` and `length` (bytes), `primary` (the place to fix when `true`), and `label` (what this place has to do with the error, or `null`).
- `suggestions[]`: `message`, `applicability`, and `edits[]`, each a span and its `replacement`. Apply an edit whose `applicability` is `MachineApplicable` as is; review a `MaybeIncorrect` one, which may change what the program means.
- `args`: facts as strings, such as the `kind`, `name`, and `owner` of a name that did not resolve.

Add `--color=never` when capturing the human form.

## `neura explain` and `neura check --fix`

- `neura explain E0441` prints the code's page: the rule, a program that breaks it, and the program fixed. `neura explain setter-never-stores` takes a warning's name, and `neura explain --list` lists every code. The pages are built into the binary.
- `neura check --fix file.nra...` applies every `MachineApplicable` suggestion in place (a missing `;`, a misspelled tag, a `#Deprecated` replacement), repeats until none applies, prints how many edits it made, and checks the result. It never edits the standard library. Use it to migrate 0.1.0 code, which omitted `;` after block-bodied declarations.

## Warnings

| Code | Name | Reported for | Usual fix |
| --- | --- | --- | --- |
| `W0001` | `deprecated` | A use of a `#Deprecated` declaration | Use the replacement the warning names; `neura check --fix` applies it when it can. |
| `W0002` | `experimental` | A use of an `#Experimental` declaration | `#Allow(experimental)` on the code that opts in. |
| `W0003` | `unused-result` | An expression statement that drops the result of a `#NoDiscard` call or `new` | Use the result, or `let _ = call();` to drop it on purpose. |
| `W0004` | `inline` | An `#Inline(always: true)` function the optimizer cannot inline | Remove `always: true`, or the reason it gives. |
| `W0005` | `setter-never-stores` | A setter that never assigns its field | Store `value` in the field, or drop the setter. |

Warnings do not fail a command. `-Wno-<name>` hides one, `-W<name>` turns it back on, `-Werror` makes every warning an error, and `--warn-library` also shows warnings inside the standard library. `#Allow(name)` and `#Deny(name)` on a declaration silence a warning there or make it an error there. The standard library tags I/O results, resource handles, `Result` and `Optional` queries, and value-returning calls `#NoDiscard`, so `file.write(bytes);` warns: write `let _ = file.write(bytes);` or handle the result.

## Common diagnostics and their fixes

Every message and code below was produced by `neura check --error-format=short` on a small program.

**Syntax**

| Diagnostic | Cause and fix |
| --- | --- |
| `E0201` `expected ';'` | A declaration without its `;`, most often `}` or `)` at the end of a function, class, or module: write `};` or `);`. `neura check --fix` inserts it. Also a `while` right after a bare `do { }` block, which is read as its condition. |
| `E0204` `expected an expression` | A `;` after a statement block (`if (x) { };`), or a trailing comma in an array or list literal. |
| `E0202` `expected '{'` | Every `if`/`while`/`for` body needs braces. |
| `E0202` `expected ':'` | Lambda parameters need types: `func(x: int) -> int => x + 1`. |
| `E0203` `expected ']'` | `new int[5]` does not exist; use a literal, a range, or `Lists.filled<T>(n, v)`. |
| `E0101` `invalid character '''` | No character literals; use strings or `uint32` scalars. |
| `E0105` `unexpected character after numeric literal` | No hex (`0xFF`) or `_` separators; write decimal. |

**Names and types**

| Diagnostic | Cause and fix |
| --- | --- |
| `E0304` `unknown name 'A'` | Not in scope: a missing import, a misspelling, an unqualified enum variant (write `Enum.A`), or a lambda calling itself. The human form suggests the closest name. |
| `E0305` `unknown or inaccessible member 'x' of C` | The member does not exist or is not `export`ed; the human form points at a private one. |
| `E0305` `type string has no members` | Reported for `text.length`: strings use `text.byte_length` (or `scalar_count()`). Other string functions come from the companion; see [stdlib-api.md](stdlib-api.md). |
| `E0302` `duplicate declaration of 'name'` | Often an import alias that clashes with a declaration; rename the alias. |
| `E0307` `ambiguous promoted name 'value_or'` | Two promoted modules export the name; qualify it through an alias. |
| `E0403` `expected string, got int32` | Wrong type, often `"text" + number` or `print_line(number)`. Convert with `.to_string()` or use `console.println("{}", number)`. |
| `E0403` `expected int64, got int32` | No implicit widening. Convert one side: `value as int64`. |
| `E0403` `expected bool, got int32` | Conditions and `!` need `bool`; write `x != 0`. |
| `E0415` `cannot convert string to int32` | Parse instead: `int32.parse(text)` returns `Result<int32>`. |
| `E0413` `operator '%' is not defined for float64` | No float remainder. Compute it with the companion's `trunc`: `x - y * (x / y).trunc()`. |
| `E0414` `indexing requires an array or list` | Strings are not indexable: `text.byte_at(i)` or `text.slice(a, b)`. |
| `E0417` `expected 1 type argument(s)` | Generic call without type arguments: `identity<int>(7)`. |
| `E0419` `qualify a generic enum variant with explicit type arguments` | `Optional<int>.Some(3)`, not `Optional.Some(3)`, when constructing. |
| `E0407` `expected 1 argument(s)` | Wrong argument count, or a constructor that takes different arguments (some library classes must be made with their module's `create`). |
| `E0408` `no matching overload of 'f'` / `E0409` `ambiguous overload of 'f'` | No alternative accepts the arguments, or no unique best one; convert an argument explicitly. |
| `E0433` `duplicate overload parameter signature for 'f'` | `int` and `int32` are the same type. |
| `E0423` `expression is not callable` | Calling a field: `buffer.size()` where `size` is now a field. Write `buffer.size`. |
| `E0422` `expected a value` | A `void` call used as a value, such as `let v = list.remove_at(i);`. Read the element first, then call `remove_at`. |
| `E0425` `empty collection literal needs an expected type` | `let xs: int[list] = [];`. |
| `E0425` `nil requires a class reference type` | `nil` only fits class and interface types; use `Optional<T>` otherwise. |
| `E0421` `'Store' is not a type` | Modules are not types; use an interface. |
| `E0405` `function may exit without returning int32` | Add a `return` on every path (or an `else`). |
| `E0410` `non-exhaustive match, missing variant 'B'` | Cover every variant or add a final `_ =>` arm. |

**Bindings, `last`, and `new`**

| Diagnostic | Cause and fix |
| --- | --- |
| `E0404` `a local binding requires an initializer` | `let x: int;` in a function. Give it a value, or use `last x: int;` and assign it once on every path. |
| `E0401` `cannot assign to const 'X'` | `const` bindings cannot be reassigned. |
| `E0401` `cannot assign twice to 'x'` | A second assignment to a `last` binding with a value, or to a `last` field that has an initializer. Use `let` if it changes. |
| `E0440` `cannot assign twice to 'x'` | A deferred `last x: T;` assigned on a path where it already is. |
| `E0441` `'x' may be used before it is assigned` | A deferred `last` read where some path skipped the assignment, such as an `if` without `else`. Assign it in every branch. |
| `E0442` `'x' is assigned inside a loop` | A `last` declared outside a loop and assigned in it. Declare it inside the loop or use `let`. |
| `E0444` `a deferred 'last' binding needs a type` | `last x;` → `last x: int;`. |
| `E0446` `'init' may end without assigning 'last' field 'name'` | Every `init` must assign every `last` field without an initializer, on every path. |
| `E0445` `cannot assign to 'last' field 'year' here` | A `last` field is assigned only by its class's `init`. |
| `E0447` `a deferred 'last' binding must be a local or an instance field` | Module, file, and static bindings need their value at the declaration. |
| `E0536` `a destructuring declaration cannot use 'last'` | `last [a, b] = ...;` → `const [a, b] = ...;`. |
| `E0402` `a captured binding cannot be reassigned` | Lambdas capture values. Share mutable state through an object: `let count = cells.of<int>(0);` (`Std.Core.Cell`) and then `count.value += 1` inside the lambda. |
| `E0420` `target-typed 'new' needs a concrete class type here` | `let a = new;` has no type to construct: annotate the binding (`let a: Point = new;`) or write `new Point()`. |
| `E0436` `3 values for 2 names` | A listed destructuring needs one value per name. |

**Classes, accessors, and anonymous classes**

| Diagnostic | Cause and fix |
| --- | --- |
| `E0504` `member 'f' does not match the interface requirement` | The implementing class member must be `export`ed and match the signature exactly. An accessor cannot implement an interface member; keep a method. |
| `E0502` `inherited member 'f' requires .override` | Use `let.override` / `const.override`. |
| `E0502` `override does not match inherited member 'f'` | Parameter and result types must match the base exactly. |
| `E0505` `cannot instantiate abstract class A with unimplemented method 'f'` | Implement every bodyless method in a subclass and construct that. |
| `E0503` `cannot inherit a constant module` | The base module was declared with `const`. |
| `E0510` `'code' is read-only outside its class` | The field has only `\| get:`. Change it through a method of the class, or add a `\| set:`. |
| `E0515` `'code' is write-only outside its class` | The field has only `\| set:`; add a `\| get:`. |
| `E0516` `accessors require an exported field` | Accessor clauses go only on `export let` instance fields. |
| `E0521` `a 'last' field cannot have a setter` | A `last` field never changes; drop the setter or make the field `let`. |
| `E0532` `field 'x' of a fields-only anonymous class is not exported` | Write `export let x: int = 0;` in the anonymous class. |

**Tags**

| Diagnostic | Cause and fix |
| --- | --- |
| `E0900` `unknown tag #Deprecate` | Misspelled; the fix suggests the registered name (`neura tags` lists them). |
| `E0901` `#Final does not apply to a function` | Each tag applies to certain declarations; see [tags.md](tags.md). |
| `E0908` `#Memoize is reserved: the compiler does not do what it promises yet` | Reserved tags are errors; the message says what to write instead. |
| `E0909` `expected a declaration after tags` | Tags cannot precede a statement, a destructuring declaration, or `_`. |
| `E0912` `class 'D' extends the final class 'B'` | `#Final` classes cannot be extended. |
| `E0916` `#NoDiscard on function 'f', which returns nothing` | Remove the tag from a `void` function. |
| `E0917` `#Pure function 'f' is not pure: it assigns the binding 'counter'` | A pure function writes no global state, makes no host request, and calls only pure functions. |
| `E0918` `field 'x' of #Immutable class 'P' can be assigned after construction` | Declare every field of an `#Immutable` class `last` (or `const`). |
| `E0921` `#Synchronized method 'init' is a constructor` | Constructors, destructors, static functions, and bodyless methods cannot be synchronized. |

**Modules, entry point, and tasks**

| Diagnostic | Cause and fix |
| --- | --- |
| `E0701` `cannot resolve import 'Acme.Nothing'` | Wrong name or missing `-I` source root. Retired `SysIo`/`Test` imports name their replacements. |
| `E0704` `let.promote requires an unannotated import alias` | Promote only `import:` expressions. |
| `E0431` `expected a local value binding` | `let.promote` or `export` inside a function; move it to file or module level. |
| `E0431` `loop control is only allowed inside a loop` | `break`/`continue` outside a loop. |
| `E0803` `the program has no entry point: no module overrides Std.Program.Entry.main` | `neura build` needs an entry module; use `check` or `test` for libraries. |
| `E0603` `spawn requires an enclosing scope block in the same function body` | Wrap the spawns in `scope { ... }` in that function. |
| `E0605` `a value of type Task<int32> cannot leave its scope: ...` | Keep task handles in locals inside the scope; do not return, store, or capture them. |
| `E0602` `a value of type int32[] is not transferable, so it cannot be given to a spawned task` | Pass transferable values (scalars, strings, tuples, channel ends, `#Immutable` objects, monitors). |
| `E0601` `the spawned call is not task-safe: it reaches 'g', which uses the reassignable binding 'counter'` | Do not touch mutable globals from tasks; pass values as arguments, or share state through a channel, a `Guarded`, or a monitor. The human form labels each call on the way. |
