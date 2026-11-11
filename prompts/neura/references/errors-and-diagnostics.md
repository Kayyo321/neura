# Errors, traps, and compiler diagnostics

Neura separates **expected failures**, which are ordinary return values (`Result<T>`, `Optional<T>`, or your own enum), from **runtime failures** (traps), which end the run immediately. There are no exceptions, no `try`/`catch`, and nothing unwinds. Specification: [Errors](https://github.com/Kayyo321/neura/blob/main/docs/lang/Errors.md).

## Contents

- [Designing for failure](#designing-for-failure)
- [Traps and exit statuses](#traps-and-exit-statuses)
- [Compiler diagnostics and their fixes](#compiler-diagnostics-and-their-fixes)

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

- Return `Result<T>` when a caller can recover; return your own enum when callers branch on several outcomes; trap (with `Std.Core.Failure.trap(message)`) only for violated preconditions, which are bugs.
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
| `Failure.trap`, failed `Std.Test` assertion, failed class cast | `explicit program failure (trap reason 3): <message>` | 134 |
| List changed during for-each | `collection modified during iteration (trap reason 5)` | 134 |
| Deadlock (every task parked) | `deadlock: all tasks are parked` | 134 |
| Heap, handle, or task limits | | 137 |
| `--max-steps` exceeded (default 100,000,000) | `step limit exceeded: 100000000 steps` | 152 |
| Invalid dispatch or malformed image | | 132 |

Load, bind, and usage errors from `neura run` itself exit with 1.

## Compiler diagnostics and their fixes

Every message below was produced by `neura check` on a small program.

| Diagnostic | Cause and fix |
| --- | --- |
| `expected string, got int32` | Wrong type, often `"text" + number` or `print_line(number)`. Convert with `.to_string()` or use `console.println("{}", number)`. |
| `expected int64, got int32` | No implicit widening. Convert one side: `value as int64`. |
| `expected bool, got int32` | Conditions and `!` need `bool`; write `x != 0`. |
| `expected 1 type argument(s)` | Generic call without type arguments: `identity<int>(7)`. |
| `qualify a generic enum variant with explicit type arguments` | `Optional<int>.Some(3)`, not `Optional.Some(3)`, when constructing. |
| `expected 1 argument(s)` / `expected 2 argument(s)` | Wrong argument count, or a constructor that takes different arguments (some library classes must be made with their module's `create`). |
| `unknown name 'A'` | Not in scope: missing import, a misspelling, an unqualified enum variant (write `Enum.A`), or a lambda calling itself. |
| `unknown or inaccessible member 'x' of C` | The member does not exist or is not `export`ed. |
| `type string has no members` | Reported for `text.length`: strings use `text.byte_length` (or `scalar_count()`). Other string functions come from the companion; see [stdlib-api.md](stdlib-api.md). |
| `type string[list] has no member 'join'` | Use `Std.Text.Strings.join(parts, ", ")`. |
| `indexing requires an array or list` | Strings are not indexable: `text.byte_at(i)` or `text.slice(a, b)`. |
| `member 'f' does not match the interface requirement` | The implementing class member must be `export`ed and match the signature exactly. |
| `inherited member 'f' requires .override` | Use `let.override` / `const.override`. |
| `override does not match inherited member 'f'` | Parameter and result types must match the base exactly. |
| `cannot instantiate abstract class A with unimplemented method 'f'` | Implement every bodyless method in a subclass and construct that. |
| `function may exit without returning int32` | Add a `return` on every path (or an `else`). |
| `a captured binding cannot be reassigned` | Lambdas capture values. Share mutable state through an object: `let count = cells.of<int>(0);` (`Std.Core.Cell`) and then `count.value += 1` inside the lambda. |
| `a local binding requires an initializer` | `let x: int = 0;`, never `let x: int;` in a function. |
| `expected a value` | A `void` call used as a value, such as `let v = list.remove_at(i);`. Read the element first, then call `remove_at`. |
| `empty collection literal needs an expected type` | `let xs: int[list] = [];`. |
| `expected an expression` | Often a trailing comma in an array or list literal. |
| `invalid character '''` | No character literals; use strings or `uint32` scalars. |
| `unexpected character after numeric literal` | No hex (`0xFF`) or `_` separators; write decimal. |
| `expected '{'` | Every `if`/`while`/`for` body needs braces. |
| `expected ';'` | A missing semicolon, or a `while` right after a bare `do { }` block being read as its condition. |
| `expected ':'` | Lambda parameters need types: `func(x: int) -> int => x + 1`. |
| `expected ']'` | `new int[5]` does not exist; use a literal, a range, or `Lists.filled<T>(n, v)`. |
| `non-exhaustive match, missing variant 'B'` | Cover every variant or add a final `_ =>` arm. |
| `nil requires a class reference type` | `nil` only fits class and interface types; use `Optional<T>` otherwise. |
| `operator '%' is not defined for float64` | No float remainder. Compute it with the companion's `trunc`: `x - y * (x / y).trunc()`. |
| `cannot convert string to int32` | Parse instead: `int32.parse(text)` returns `Result<int32>`. |
| `cannot assign to const 'X'` | `const` bindings cannot be reassigned. |
| `duplicate declaration of 'name'` | Often an import alias that clashes with a declaration; rename the alias. |
| `ambiguous overload of 'f'` | No unique best overload; convert an argument explicitly. |
| `duplicate overload parameter signature for 'f'` | `int` and `int32` are the same type. |
| `ambiguous promoted name 'value_or'` | Two promoted modules export the name; qualify it through an alias. |
| `let.promote requires an unannotated import alias` | Promote only `import:` expressions. |
| `expected a local value binding` | `let.promote` or `export` inside a function; move it to file or module level. |
| `'Store' is not a type` | Modules are not types; use an interface. |
| `cannot inherit a constant module` | The base module was declared with `const`. |
| `loop control is only allowed inside a loop` | `break`/`continue` outside a loop. |
| `cannot resolve import 'Acme.Nothing'` | Wrong name or missing `-I` source root. Retired `SysIo`/`Test` imports name their replacements. |
| `the program has no entry point: no module overrides Std.Program.Entry.main` | `neura build` needs an entry module; use `check` or `test` for libraries. |
| `spawn requires an enclosing scope block in the same function body` | Wrap the spawns in `scope { ... }` in that function. |
| `a value of type Task<int32> cannot leave its scope: ...` | Keep task handles in locals inside the scope; do not return, store, or capture them. |
| `a value of type int32[] is not transferable, so it cannot be given to a spawned task` | Pass transferable values (scalars, strings, tuples, channel ends). |
| `the spawned call is not task-safe: it reaches 'f' ..., and that code uses the reassignable binding 'x'` | Do not touch mutable globals from tasks; pass values as arguments or use a channel or `Guarded`. |
