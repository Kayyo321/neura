# Core language

This page defines the initial core. See [Feature status](Status.md) for the frontend’s coverage and open semantic decisions.

## Source and bindings

Source files are UTF-8; `//` introduces a comment through the end of the line. Identifiers use letters, digits and underscores and cannot start with a digit. Keywords are reserved. A binding is introduced by `let`, `const`, or `last`, with an optional type annotation, and ends with `;`. The exact Unicode identifier and escape rules remain open.

| Introducer | Assigned | Where |
| --- | --- | --- |
| `let` | at its declaration, then any number of times | everywhere |
| `const` | once, at its declaration | everywhere; the form for module, file, and static constants |
| `last` | exactly once: at its declaration, or later on every path before it is read | everywhere with a value; without one, only a local or an instance field |

None of them prevents mutation through a reference: a `const` or `last` binding to an object can still change the object's fields. A binding must be assigned before it is read.

<!-- example: check -->
```neura
let count: int32 = 0;
const Limit: int32 = 3;
count = count + 1;

let describe = func(value: int32) -> string {
    last sign: string;
    if (value < 0) {
        sign = "negative";
    } else {
        sign = "not negative";
    }
    last text = sign + " " + value.to_string();
    return text;
};
```

`last x = e;` and `last x: T = e;` behave like a local `const`: assigning `x` again is error `E0401`. `last x: T;` declares `x` without a value; its type is required (`E0444`). `last` is a contextual word, not a keyword. It introduces a declaration only where a statement or member can start and an identifier follows it, as in `last x`, `export last x`, and `static last x`. Elsewhere it is an ordinary name, so `let last = func ...;`, `last = 3;`, `last(x);`, `last.next`, and `last[0] = 1;` keep their meaning. A destructuring `last [a, b] = e;` is not a declaration: it reads as an assignment to an element of `last`.

An annotation names the type, so the value need not repeat it. `new` without a class constructs the annotated type ([target-typed `new`](ObjectLifecycle.md#target-typed-new)), a destructuring declaration can annotate its names with a tuple type and give each name its own value ([typed and listed destructuring](Types.md#typed-and-listed-destructuring)), and a class body can be written where a type is expected ([anonymous classes](Types.md#anonymous-classes)).

<!-- example: check -->
```neura
let Counter = class (
    export let count: int = 0;
);

let first: Counter = new;
let [low, high]: [Counter, int] = new, 10;
let point: class (
    export let x: int = 0;
    export let y: int = 0;
) = new;
```

Bindings are visible from their declaration to the end of their enclosing block; nested blocks introduce scopes. Duplicate names in the same scope are errors. A nested block, and a function body, may declare a name that an enclosing scope or a parameter already has; the new binding hides the outer one for the rest of its block. Whether shadowing stays allowed is an open language decision ([Status](Status.md#open-language-decisions)). Exported module and class members form their public interface; unexported members are accessible within their declaring module or class. Visibility and inheritance rules beyond these are not specified.

### Deferred assignment

A local `last x: T;` is assigned by a plain assignment `x = value;`. The compiler checks each such binding with a definite-assignment analysis over the function body, before lowering. At each point the binding is unassigned, assigned, or assigned on some paths but not others. Branches merge as follows: after an `if` with an `else`, a `match`, or a `select`, a binding is assigned when every arm that can reach the end of the statement assigned it. An arm that ends in `return`, `break`, `continue`, or `trap_with_message(...)` does not reach the end. An `if` without an `else` merges with the path on which its condition is false. A Boolean literal condition selects one arm, so `while (true)` without a `break` does not end. A loop leaves the state as it was before the loop.

- **Read.** Reading `x`, including in `x += 1`, `x++`, or `x.field`, requires `x` to be assigned on every path to the read (`E0441`). The error points at the declaration and names a branch that skips the assignment, such as an `if` condition that can be false or a `match` arm.
- **Assign.** Assigning `x` where it is, or may be, already assigned is an error (`E0440`), and so is `x += value` or `x++` after the assignment.
- **Loops.** An assignment inside a loop body, condition, or step to a `last` binding declared outside that loop is an error (`E0442`), because the loop could run it twice. This holds even when a `break` follows the assignment. A binding declared inside the loop body is a new binding on each iteration and can be assigned there.
- **Closures.** A lambda copies the locals it captures when it is created, so a lambda that uses `x` must be created where `x` is assigned on every path (`E0443`). A lambda cannot assign a captured binding (`E0402`).
- **Scope.** The rules apply from the declaration to the end of its block. A name declared in an inner block, a parameter of a nested lambda, a `match` arm binding, or a for-each element hides `x`.

The analysis follows the syntax, not values: it treats every condition other than a Boolean literal as possibly true and possibly false, and a `&&` or `||` right operand as possibly skipped. A binding that is declared and never read need not be assigned. NeuraIR records a deferred local as a declaration without a value ([NeuraIR 1.10](../NirFormat.md#deferred-initialization)); the runtime traps a read before the first store in a frame, which the analysis rules out.

A `last` instance field follows the same rules within each `init`; [Object lifecycle](ObjectLifecycle.md#last-fields) states them. A module, file, or static binding cannot be deferred (`E0447`).

### Documentation comments

Start each line of a documentation comment with `///` and place it immediately above the definition it describes. Consecutive documentation lines form one block. An empty `///` line adds a paragraph break. A blank source line or an ordinary `//` comment ends the block. An inline comment after code has ordinary comment behavior.

<!-- example: check -->
```neura
/// @brief Doubles an integer.
/// @param value The input to double.
/// @return The result of adding the input to itself.
let twice = func(value: int) -> int => value + value;
```

Documentation attaches to bindings, functions, modules, classes, interfaces, enums and their variants, and class casts. Place a separate block immediately above the `or` of an overload alternative to document that signature. Destructured bindings share the comment above the declaration. These comments have no effect on type checking or execution.

`neura docs <file.nra>... [-o <index.json>]` exports a versioned JSON index for editor integrations. It parses the named sources, so imports can remain unresolved. Each file contains the documented symbols with their scope, signature, declaration range, selection range, and documentation. Ranges carry a one-based line and Unicode scalar column plus a zero-based UTF-8 byte offset and byte length. The index preserves raw comment text after stripping the `///` prefix and one following space. It also records line-start Doxygen commands beginning with `@` or `\`, including parameter subjects and directions. [Doxygen documentation blocks](https://www.doxygen.nl/manual/docblocks.html)

To generate documentation with Doxygen itself, use `neura docs --doxygen <file.nra>` as its input filter. The filter emits declaration stubs at their original source lines and retains attached comments. The JSON index also includes function-local definitions and Neura-formatted signatures.

```text
FILE_PATTERNS     = *.nra
EXTENSION_MAPPING = nra=C++
INPUT_FILTER      = "neura docs --doxygen"
EXTRACT_ALL       = YES
```

The filter represents modules as namespaces and uses named placeholders for Neura types such as arrays and lists. Use the original source or the JSON index for Neura-specific syntax and local definitions. [Doxygen input filters](https://www.doxygen.nl/manual/config.html#cfg_input_filter)

## Functions and control flow

Function parameters use `name: Type`. Arguments are evaluated left to right. A function with no parameters may omit `()`, and a `void` function may omit `-> void`. A block body uses `return` to exit the function. A non-`void` function must return a value on every reachable path. A `void` function may fall through.

A [primitive reference parameter](Types.md#primitive-references) such as `value: &int` receives an alias to the caller's storage. Its argument uses the ordinary expression spelling, such as `increment(count)`.

<!-- example: check -->
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

`=> expression` gives a function an expression body. The expression supplies the result of a non-`void` function. A `void` function evaluates the expression for its effects.

Every declaration ends with `;`, whatever its value: an expression, a function with an expression or block body, a class, or a module. Statement blocks, such as the bodies of `if`, `while`, `for`, `match`, and `scope`, and a bare `{ }` block, do not take a `;`.

### Function overloads

Join alternatives with `or func` in one file, module, or class declaration. Each alternative has its own parameters, result, and body. They share the binding name and visibility.

<!-- example: check -->
```neura
let console = import:Std.Io.Console;

let write = func(text: string) {
    write(text, text.byte_length);
} or func(text: string, byte_count: int) {
    console.print_line(text.slice(0, byte_count));
};
```

An overload group is one declaration, so it ends with `;` after its final alternative.

The compiler first filters by argument count. With several candidates of the same arity, it ranks each argument's match in this order.

1. An exact type match. Numeric literals prefer their default width when the value fits. An existing primitive reference is an exact match for its reference type.
2. An assignable type, such as a class upcast. A more specific parameter type wins between assignable alternatives.
3. Binding a primitive reference to an ordinary mutable place with the same primitive type.
4. A contextual literal match. Numeric and nested collection literals must fit the candidate's types. For `nil`, a more specific reference type wins.
5. One visible [object cast](Types.md#object-casts) to the parameter type.

One candidate beats another if it ranks at least as high on every argument and higher on at least one. The compiler selects the unique candidate that no other candidate beats. A tie or conflicting preferences produce an ambiguity error. Declaration order and expected result type play no part. The receiver and arguments are evaluated in the usual order, with any selected cast run at its argument's position.

Distinct alternatives must have distinct resolved parameter types or counts. For example, `func(value: int)` and `func(other: int32)` in one group are duplicates because `int` aliases `int32`. Alternatives require bodies and fixed arity and take no function type parameters. They may use the type parameters of an enclosing class. Call a group directly or use a lambda to select an alternative for a function value. [Constructor overloads](ObjectLifecycle.md#construction) use the same selection rules.

### Variadic parameters

A named function can mark its final parameter `items: ...T` to accept zero or more arguments assignable to `T`. Each call evaluates its fixed arguments and then its variadic arguments in source order. The function receives a new `T[]`. Reading a named variadic function as a function value exposes its packed signature, so an indirect call supplies the final array explicitly.

<!-- example: check -->
```neura
let count = func(items: ...int) -> int => items.length;
let three = count(10, 20, 30);
```

A bare `items: ...` packs mixed types into an array of `Std.Core.Variadic.Argument`. The variants retain strings, booleans, each floating-point width, and object references. Integers are widened to signed or unsigned 64-bit values. Other values with a built-in `to_string()` use that text representation. Pass an explicit representation for types without a value member. An existing `Argument` is passed through as-is. The standard library module also exposes the type for callers that want to match the variants or construct a pack directly.

`if (condition) { ... } else { ... }` selects a branch; `while (condition) { ... }` repeats while its Boolean condition is true. `break` exits the innermost loop and `continue` starts its next iteration. Conditions must be `bool`; there is no implicit truthiness. See [core.nra](../../examples/guide/core.nra).

### Function values and closures

A lambda is a function expression with typed parameters and a body. Its type lists the parameter types in order, followed by the result. An invocation evaluates the callable expression first and its arguments from left to right.

<!-- example: check -->
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

<!-- example: check -->
```neura
let console = import:Std.Io.Console;

let sum_to = func(limit: int32, names: string[list]) -> int32 {
    let total: int32 = 0;
    let current: int32 = 0;

    do {
        total += current++;
    } while (current <= limit);

    for (let i: int32 = 0; i < limit; i++) {
        total += i;
    }

    for (let name in names) {
        console.print_line(name);
    }
    return total;
};
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

## Tasks, scopes, and select

A call can run as a *task*: an independently suspendable call that shares no mutable state with the task that started it. Three forms start tasks, wait for them, and choose between waits. [Concurrency](Concurrency.md) specifies them together with the runtime, the library, and the status of each part.

`scope`, `spawn`, and `select` are contextual words. The lexer produces identifiers for them, and the parser reads them as these forms only in the positions below. Everywhere else they are ordinary names, so `let scope = 3;`, `spawn(x)`, and a module member named `select` keep their meaning.

```text
scope-statement  = "scope" [ "(" identifier ")" ] block
spawn-expression = "spawn" call
select-statement = "select" "{" [ arm { "," arm } [ "," ] ] "}"
arm              = ( [ ( "let" | "const" ) identifier "=" ] call | "_" ) "=>" block
```

| Word | Is a form when | Otherwise |
| --- | --- | --- |
| `scope` | It begins a statement and the next token is `{`, or the next tokens are `(`, one identifier, `)`, and `{`. | A name. `scope(name);` is a call, because `;` follows, and `scope = 3;` is an assignment. |
| `spawn` | It begins an expression and the next token is an identifier. | A name. `spawn(x)` is a call of a function named `spawn`; so is `spawn (f)(x)`. |
| `select` | It begins a statement and the next token is `{`. | A name. |

A `scope` statement is allowed only in a function body: a function, a method, or a lambda. In a top-level statement or an initializer it is an error, because globals are still being initialized.

### Scope

`scope { ... }` runs an ordinary block with one added rule. Every exit from it, whether by reaching the end, `return`, `break`, or `continue`, first waits until every task spawned in the block, and every task those tasks spawned, has finished. A `return` evaluates its expression before it waits. Waiting does not cancel a task. Scopes nest, and a task belongs to the nearest enclosing scope of its `spawn`. No task outlives its scope, so none outlives `main`. A runtime failure in any task ends the run at once without waiting ([Errors](Errors.md)).

`scope (name) { ... }` also binds `name`, a constant of type `Std.Concurrent.Scope`, in the block. Its `cancel()` requests cancellation of the scope's tasks, `is_cancelled()` reports it, and `cancel_after`, `cancel_at`, and `deadline()` set and read a deadline. Cancellation is cooperative: it wakes a task that is parked on a channel operation, a timer, or a cancellable host request with a `Cancelled` result, and a running task learns of it by asking `Std.Concurrent.Cancellation.requested()`. Nothing interrupts a running task, and `join` is never cancelled.

### Spawn

`spawn call` starts a task and gives the parent a `Std.Concurrent.Task<T>`, where `T` is the call's result type. A call of a `void` function gives `Task<Unit>`, with `Unit` and its single value `Unit.Value` from `Std.Core`. The operand must begin with a name and be a call of a function, a method, or a function value: `fetch(id)`, `client.fetch(id)`, `Jobs.twice(21)`, or `handler(request)`. A lambda is bound to a name first. The `spawn` must be inside a `scope` of the same function body; a `spawn` in a lambda written inside the scope is not.

The parent evaluates the callee, a method's receiver, and then the arguments from left to right, as it would for the call itself, including overload resolution, casts, and variadic packing. It then creates the task, appends it to the queue of runnable tasks, and continues. `spawn` does not switch tasks: the child first runs when the parent next waits, or when its turn of language operations ends ([Concurrency](Concurrency.md#102-the-reference-scheduler)). A `spawn` used as a statement produces no handle.

`task.join()` waits until the task has finished and returns its result; joining again returns the same value. `task.is_finished()` does not wait. A scope can exit without joining, and the result is then discarded. A task reports an expected failure as an ordinary returned value, such as a `Result`, and `join` hands it to the caller.

<!-- example: check -->
```neura
let console = import:Std.Io.Console;

let say = func(text: string) -> int {
    console.print_line(text);
    return text.byte_length;
};

let report = func -> int {
    scope {
        let left = spawn say("left task");
        let right = spawn say("right task");
        console.print_line("parent continues");
        return left.join() + right.join();
    }
};
```

At the default quantum, `report()` prints `parent continues`, then `left task` and `right task`, and returns 19: the parent reaches its first wait at `left.join()`, and only then do the two tasks run, in the order they were spawned.

### Select

`select { ... }` waits on several operations and performs exactly one. An arm is `[let name =] operation => { block }`, or `_ => { block }`, the default arm, which may appear once. The operation is a call of `receive`, `send` (with the value as its argument), `join`, or `wait` on a channel end, a task, or a timer from `Std.Concurrent`. A `const` binding is constant, the name `_` discards the result, and the binding receives what the same call would return on its own. A select needs at least one arm that waits and is allowed in any function body.

- The receiver and arguments of every arm are evaluated once, in source order, before anything is attempted. The value of an arm that does not commit is evaluated and dropped.
- Exactly one arm commits, and only its block runs. No other arm has any effect.
- **Rotating preference.** Each task counts the selects it has committed. A select with `n` listed arms scans from arm `count mod n` cyclically, and the first arm that is ready commits. A send arm is ready when the send would complete.
- When no arm is ready, the default arm runs if there is one. Otherwise the task waits on every arm, and the first operation that completes for it commits.
- When the task's scope is cancelled and nothing is ready and there is no default arm, a channel or timer arm commits `Cancelled`. A `join` arm is not cancelled.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let channels = import:Std.Concurrent.Channel;

let drain = func(inbox: channels.Receiver<int>, spare: channels.Receiver<int>) {
    for (let round: int = 0; round < 3; round++) {
        select {
            let first = inbox.receive() => {
                match (first) {
                    channels.Received.Value(number) => { console.print_line("inbox " + number.to_string()); },
                    _ => { console.print_line("inbox ended"); },
                }
            },
            let second = spare.receive() => {
                match (second) {
                    channels.Received.Value(number) => { console.print_line("spare " + number.to_string()); },
                    _ => { console.print_line("spare ended"); },
                }
            },
            _ => {
                console.print_line("nothing ready");
            },
        }
    }
};

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let [inbox_sender, inbox, inbox_closer] = channels.open<int>(1);
        let [spare_sender, spare, spare_closer] = channels.open<int>(1);
        inbox_sender.send(1);
        spare_sender.send(2);
        drain(inbox, spare);
        return program.ExitCode.Ok;
    };
);
```

```text
inbox 1
spare 2
nothing ready
```

With one value waiting on each channel, `drain` prints `inbox 1`, `spare 2`, and `nothing ready`: the preference moves to the second arm after the first select commits.

### Scope-bound values

A `Task<T>` or `Scope`, and an array, list, or tuple of them, is *scope-bound* and cannot be reachable after its scope exits. It can be held in a local declared inside the scope, including a local list, and passed as an argument to a call. It cannot be returned, stored in a field, global, or enum payload, captured by a lambda, passed to `spawn`, sent on a channel, used as a type argument, or assigned to a binding declared outside the scope. [Types](Types.md#scope-bound-and-transferable-types) lists the rules, the types that can cross between tasks, and the calls a task may make.

<!-- example: check -->
```neura
let cancellation = import:Std.Concurrent.Cancellation;

let count_until_stopped = func -> int {
    let laps = 0;
    while (!cancellation.requested()) {
        laps = laps + 1;
    }
    return laps;
};

let stop_early = func -> bool {
    scope (group) {
        let counter = spawn count_until_stopped();
        group.cancel();
        return counter.join() >= 0;
    }
};
```

`count_until_stopped` loops until `Cancellation.requested()` is true. The scope's handle `group` is a scope-bound value used inside the block, and `stop_early` returns once the task has seen the request and finished. Because the task has not yet run when `group.cancel()` is called, it starts with the request already visible and returns 0.

The compiler rejects, at the span of the offending expression, a `spawn` outside a `scope` of its function body, an operand that is not a call or does not begin with a name, a scope-bound value that leaves its scope, and a `scope` outside a function body. See [concurrency example](../../examples/concurrency/service.nra) for a complete program.

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
15. `..` and its optional step in [range expressions](Collections.md#ranges)
16. `? :`

Assignments are statements. `=` stores a value and `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `^=`, `|=`, `<<=`, and `>>=` apply the corresponding binary operator before storing its result. Each compound assignment evaluates the destination's receiver and index once, reads the old value, then evaluates the right operand and stores the result. An operation that traps leaves the final store unexecuted.

Prefix and postfix increment and decrement require a mutable numeric binding, field, or element. They evaluate the destination once and add or subtract a one of its type. `++x` and `--x` yield the updated value. `x++` and `x--` yield the previous value. The usual arithmetic rules apply, including checked integer overflow. [Numbers](Numbers.md) specifies integer and floating-point arithmetic. `+` and `+=` can also concatenate strings.

`a === b` calls `a.equals(b)` and requires its result to be Boolean. This uses the normal method selection and dispatch rules, including class overrides and value members on built-in types.

A conditional expression `condition ? when_true : when_false` evaluates its Boolean condition and then evaluates only the selected arm. Both arm types must be assignable to the expression's expected type when one is available. Otherwise the first arm supplies the type, with contextual typing from the second arm for a first-arm `nil` or a numeric literal paired with a nonliteral. The conditional operator associates to the right.

### Comparison chains

A comparison can use one subject and several alternatives. For example, `x == 0 or 1 or 2` tests whether `x` equals any of those values, and `x != 0 and 1` tests whether it differs from both. The subject is evaluated once. Each alternative is evaluated left to right and compared with the captured subject using the original operator. An `or` chain stops at the first true comparison. An `and` chain stops at the first false comparison.

The chain consumes its alternatives before the surrounding `&&` or `||`. An alternative can be an arithmetic or bitwise expression. Use parentheses around comparisons that follow a chain connector. Each chain uses a single connector throughout. Join independent comparisons with `&&` or `||`, as in `x == 0 && y == 1`.
