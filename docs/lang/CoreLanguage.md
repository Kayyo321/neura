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

Function parameters use `name: Type`. Arguments are evaluated left to right. A function with no parameters may omit `()`, and a `void` function may omit `-> void`. A block body uses `return` to exit the function. A non-`void` function must return a value on every reachable path. A `void` function may fall through.

```neura
let sum_to = func(limit: int32) -> int32 {
    let total: int32 = 0;
    let current: int32 = 1;
    while (current <= limit) {
        total = total + current;
        current = current + 1;
    }
    return total;
};

let twice = func(value: int) -> int => value * 2;
let forty_two = func -> int => twice(21);
let reset = func {
    // A zero-parameter void function.
};
```

`=> expression` gives a function an expression body. The expression supplies the result of a non-`void` function. A `void` function evaluates the expression for its effects. A named declaration with an expression body ends with `;`.

`if (condition) { ... } else { ... }` selects a branch; `while (condition) { ... }` repeats while its Boolean condition is true. `break` exits the innermost loop and `continue` starts its next iteration. Conditions must be `bool`; there is no implicit truthiness. See [core.nra](../../examples/guide/core.nra).

### Function values and closures

A lambda is a function expression with typed parameters and a body. Its type lists the parameter types in order, followed by the result. An invocation evaluates the callable expression first and its arguments from left to right.

```neura
let make_adder = func(offset: int) -> func(int) -> int {
    return func(value: int) -> int {
        return value + offset;
    };
};

let add_two = make_adder(2);
let result = add_two(40);
```

Functions with the same parameter and result types have the same function type. A function value can be assigned, passed as an argument, returned, and stored in a field or collection. Parenthesize the function type before a collection suffix to write an array of functions, such as `(func(int) -> int)[]`. A named non-generic function used as a value is callable through the same interface. Reading a class or interface method as a value binds its receiver and keeps normal virtual dispatch.

A lambda captures the values of enclosing parameters and local bindings it uses when it is created. A captured binding cannot be reassigned within the lambda. Capturing an object or collection keeps a reference to the same instance, so updates to its contents are visible. A lambda in a class method can use its enclosing receiver, and a nested lambda can capture values available to its enclosing lambda. Module and file globals are read when the body runs. Closures may outlive the scope that created them.

Signatures, captures, and local types in a closure can mention the type parameters of the enclosing function, class, or method. The compiler specializes the function type and capture layout with that declaration. Call a generic function with explicit type arguments. Use a named function declaration when a function needs to call itself by name. A closure resolves implicit module members in the enclosing declaration's module.

## Loops

Neura supports pre-test, post-test, infinite, three-part, and for-each loops. The `for`-each form accepts an array, list, or an implementation of `Iterable<T>`.

```neura
do {
    total += current++;
} while (current <= limit);

for (let i: int32 = 0; i < limit; i++) {
    total += i;
}

for (let name in names) {
    console.print_line(name);
}
```

**Post-test and infinite loops.** `do { body } while (condition);` runs the body at least once. The Boolean condition is evaluated after the body falls through or executes `continue`. The body's local bindings are scoped to the body. A `break` or `return` skips the condition. With no trailing `while` clause, `do { body }` repeats indefinitely until a `break`, a `return`, or a trap.

**Three-part `for`.** `for (initializer; condition; step) { body }` runs the initializer once, then tests the condition before each iteration and runs the step after each iteration. The initializer is a binding, an assignment, an expression, or empty; a binding declared there is visible in the condition, the step, and the body, and not after the loop. The condition must be `bool`; when it is omitted, as in `for (;;)`, it is `true`. The step is an assignment, an expression, or empty, and cannot contain `break`, `continue`, or `return`. `continue` runs the step and then tests the condition; `break` leaves without running the step.

**For-each.** `for (let name in collection) { body }` visits the elements of an array or list in index order. `const` instead of `let` makes the element binding constant. An optional annotation, `let name: Type`, must accept the element type. The element binding is scoped to the body, and the body cannot declare another binding with its name.

- The collection expression is evaluated once, before the first iteration.
- Each iteration reads the element at the next index, binds it, and then runs the body. `continue` advances to the next element; `break` and `return` leave the loop without advancing.
- Assigning to an element by index during the loop is allowed. A later iteration sees the new value.
- For a list, the loop captures the list's *structural version* when it starts. Every successful `append` or `remove_at` on the list, through any alias, changes the version. Before each advance, including the one that would end the loop, the loop compares the current version with the captured one. If they differ, the run traps at the `for` statement with trap reason 5 ("collection modified during iteration"). A removal followed by an append therefore traps even though the length is unchanged. Arrays cannot change length, so a for-each over an array needs no check.

These loop forms lower to NeuraIR's loop and branch operations. See [Backend](../Backend.md#loops).

For an `Iterable<T>`, the loop evaluates its source once and calls `iterator()` once. It calls the iterator's `has_next()` before each advance, and calls `next()` to get each element. A `continue` returns to `has_next()`. A `break` or `return` leaves without another call. A library iterator enforces its collection's modification rules in these methods.

## Expressions

Arithmetic operands must have the same type. Remainder applies to integers. Boolean operators short-circuit and evaluate left to right. Parentheses group expressions. The operator levels from tightest to loosest are

1. member access, indexing, and calls
2. postfix `++` and `--`
3. prefix `++`, `--`, `!`, `-`, and `~`
4. `as` and `is`
5. `*`, `/`, `%`
6. `+`, `-`
7. `<<`, `>>`
8. `&`
9. `^`
10. `|`
11. `<`, `<=`, `>`, `>=`
12. `==`, `!=`, `===`
13. `&&`
14. `||`
15. `? :`

Assignments are statements. `=` stores a value and `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `^=`, `|=`, `<<=`, and `>>=` apply the corresponding binary operator before storing its result. Each compound assignment evaluates the destination's receiver and index once, reads the old value, then evaluates the right operand and stores the result. An operation that traps leaves the final store unexecuted.

Prefix and postfix increment and decrement require a mutable numeric binding, field, or element. They evaluate the destination once and add or subtract a one of its type. `++x` and `--x` yield the updated value. `x++` and `x--` yield the previous value. The usual arithmetic rules apply, including checked integer overflow. [Numbers](Numbers.md) specifies integer and floating-point arithmetic. `+` and `+=` can also concatenate strings.

`a === b` calls `a.equals(b)` and requires its result to be Boolean. This uses the normal method selection and dispatch rules, including class overrides and value members on built-in types.

A conditional expression `condition ? when_true : when_false` evaluates its Boolean condition and then evaluates only the selected arm. Both arm types must be assignable to the expression's expected type when one is available. Otherwise the first arm supplies the type, with contextual typing from the second arm for a first-arm `nil` or a numeric literal paired with a nonliteral. The conditional operator associates to the right.

### Comparison chains

A comparison can use one subject and several alternatives. For example, `x == 0 or 1 or 2` tests whether `x` equals any of those values, and `x != 0 and 1` tests whether it differs from both. The subject is evaluated once. Each alternative is evaluated left to right and compared with the captured subject using the original operator. An `or` chain stops at the first true comparison. An `and` chain stops at the first false comparison.

The chain consumes its alternatives before the surrounding `&&` or `||`. An alternative can be an arithmetic or bitwise expression. Use parentheses around comparisons that follow a chain connector. Each chain uses a single connector throughout. Join independent comparisons with `&&` or `||`, as in `x == 0 && y == 1`.
