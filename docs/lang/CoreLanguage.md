# Core language

This page defines the initial core. See [Feature status](Status.md) for the frontend’s coverage and open semantic decisions.

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

### Function values and closures

A lambda is a function expression with typed parameters and a body. Its type lists the parameter types in order, followed by the result. An invocation evaluates the callable expression first and its arguments from left to right.

```neura
let make_adder = func(int offset) -> func(int) -> int {
    return func(int value) -> int {
        return value + offset;
    };
};

let add_two = make_adder(2);
let result = add_two(40);
```

Functions with the same concrete parameter and result types have the same function type. A function value can be assigned, passed as an argument, returned, and stored in a field or collection. Parenthesize the function type before a collection suffix to write an array of functions, such as `(func(int) -> int)[]`. A named non-generic function used as a value is callable through the same interface. Reading a class method as a value binds its receiver and keeps normal virtual dispatch.

A lambda captures the values of enclosing parameters and local bindings it uses when it is created. A captured binding cannot be reassigned within the lambda. Capturing an object or collection keeps a reference to the same instance, so updates to its contents are visible. A lambda in a class method can use its enclosing receiver, and a nested lambda can capture values available to its enclosing lambda. Module and file globals are read when the body runs. Closures may outlive the scope that created them.

Function-value signatures and captured types must be concrete. Use a direct call with explicit type arguments for a generic function. Use a named function declaration when a function needs to call itself by name. A closure resolves implicit module members in the enclosing declaration's module.

## Loops

Neura has three loop statements: `while`, the three-part `for`, and `for`-each over arrays and lists.

```neura
for (let i: int32 = 0; i < limit; i = i + 1) {
    total = total + i;
}

for (let name in names) {
    io.println(name);
}
```

**Three-part `for`.** `for (initializer; condition; step) { body }` runs the initializer once, then tests the condition before each iteration and runs the step after each iteration. The initializer is a binding, an assignment, an expression, or empty; a binding declared there is visible in the condition, the step, and the body, and not after the loop. The condition must be `bool`; when it is omitted, as in `for (;;)`, it is `true`. The step is an assignment, an expression, or empty, and cannot contain `break`, `continue`, or `return`. `continue` runs the step and then tests the condition; `break` leaves without running the step.

**For-each.** `for (let name in collection) { body }` visits the elements of an array or list in index order. `const` instead of `let` makes the element binding constant. An optional annotation, `let name: Type`, must accept the element type. The element binding is scoped to the body, and the body cannot declare another binding with its name.

- The collection expression is evaluated once, before the first iteration.
- Each iteration reads the element at the next index, binds it, and then runs the body. `continue` advances to the next element; `break` and `return` leave the loop without advancing.
- Assigning to an element by index during the loop is allowed. A later iteration sees the new value.
- For a list, the loop captures the list's *structural version* when it starts. Every successful `append` or `remove_at` on the list, through any alias, changes the version. Before each advance, including the one that would end the loop, the loop compares the current version with the captured one. If they differ, the run traps at the `for` statement with trap reason 5 ("collection modified during iteration"). A removal followed by an append therefore traps even though the length is unchanged. Arrays cannot change length, so a for-each over an array needs no check.

Both loops lower to NeuraIR's existing loop and branch operations ([Backend](../Backend.md#loops)).

## Expressions

The operators include arithmetic `+ - * / %`, comparisons `== != < <= > >=`, Boolean `&& || !`, integer bitwise operators `~ & ^ | << >>`, unary `-`, the conversion `as`, and assignment `=`. Arithmetic operands must have the same type. Remainder applies to integers. Boolean operators short-circuit and evaluate left to right. Parentheses group expressions. Precedence, from tightest to loosest:

1. member access, indexing, and calls
2. unary `!`, `-`, and `~`
3. `as`
4. `*`, `/`, `%`
5. `+`, `-`
6. `<<`, `>>`
7. `&`
8. `^`
9. `|`
10. `<`, `<=`, `>`, `>=`
11. `==`, `!=`
12. `&&`
13. `||`

Assignment is a statement, not a value. [Numbers](Numbers.md) specifies integer and floating-point literals, arithmetic, overflow, division, rounding, and conversions. String concatenation with `+` accepts two strings; implicit conversion to string is not specified.

### Comparison chains

A comparison can use one subject and several alternatives. For example, `x == 0 or 1 or 2` tests whether `x` equals any of those values, and `x != 0 and 1` tests whether it differs from both. The subject is evaluated once. Each alternative is evaluated left to right and compared with the captured subject using the original operator. An `or` chain stops at the first true comparison. An `and` chain stops at the first false comparison.

The chain consumes its alternatives before the surrounding `&&` or `||`. An alternative can be an arithmetic or bitwise expression. Use parentheses around comparisons that follow a chain connector. Each chain uses a single connector throughout. Join independent comparisons with `&&` or `||`, as in `x == 0 && y == 1`.
