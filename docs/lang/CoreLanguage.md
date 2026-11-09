# Core language

This is a proposed initial core. Examples are illustrative and not compiler-verified.

## Source and bindings

Source files are UTF-8; `//` introduces a comment through the end of the line. Identifiers use letters, digits and underscores and cannot start with a digit. Keywords are reserved. A binding is introduced by `let` or `const`, with an optional type annotation. `let` permits reassignment; `const` prevents reassignment, not mutation through a reference. A binding must be initialized before it is read. The exact Unicode identifier and escape rules remain open.

```neura
let count: int32 = 0;
const Limit: int32 = 3;
count = count + 1;
```

Bindings are visible from their declaration to the end of their enclosing block; nested blocks introduce scopes. Duplicate names in the same scope are errors. Shadowing policy remains open. Exported module and class members form their public interface; unexported members are accessible within their declaring module or class. More detailed visibility and inheritance rules remain open.

## Functions and control flow

Functions declare parameter and return types. Arguments are evaluated left to right; `return` exits the function. A non-`void` function must return a value on every reachable path. A `void` function may fall through. Function overloading and default arguments are not specified.

```neura
let sum_to = func(int32 limit) -> int32 {
    let total: int32 = 0;
    let current: int32 = 1;
    while (current <= limit) {
        total = total + current;
        current = current + 1;
    }
    return total;
};
```

`if (condition) { ... } else { ... }` selects a branch; `while (condition) { ... }` repeats while its Boolean condition is true. `break` exits the innermost loop and `continue` starts its next iteration. Conditions must be `bool`; there is no implicit truthiness. See [core.nra](../../examples/guide/core.nra).

## Expressions

The initial operators are arithmetic `+ - * /`, comparisons `== != < <= > >=`, Boolean `&& || !`, and assignment `=`. Arithmetic operands must have compatible types. Boolean operators short-circuit and evaluate left to right. Parentheses group expressions; the intended precedence from tightest to loosest is unary, multiplication/division, addition/subtraction, ordering, equality, `&&`, `||`, assignment. Assignment is a statement, not a value. Integer overflow, division by zero, floating-point behavior, and the complete operator and literal grammar require specification before implementation. String concatenation with `+` accepts two strings; implicit conversion to string is not specified.
