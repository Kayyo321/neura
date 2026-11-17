# Neura 0.3.0: class hooks and startup blocks

This note fixes the design of the 0.3.0 milestone that adds class-level read and admission hooks and the startup block `static scope`. A class hook runs whenever a reference of that class is read from storage or admitted to storage, a parameter or a result; it is a rule about a class's references everywhere, where a [field accessor](../lang/Types.md#accessors) is a rule about one field. The note builds on [semantic facts and representations](Representation030.md), which reserved the hook fact and the policy provenance of a reference, and on [storage and construction](Storage030.md), whose value words carry that provenance. It is a design note in the sense of [the 0.3.0 baseline](Release030.md). Rows move to [Types](../lang/Types.md), [Modules](../lang/Modules.md), [NVR](../runtime/NVR.md) and [NVM](../runtime/NVM.md) in the change that implements them.

Status: *proposed* on 2026-11-17. The owner was away and had asked for autonomous work toward 0.3.0, so the rows are implemented as written by autonomous agents and await the owner's confirmation. Later changes to a row are made here, with the reason.

A program that declares no class hook and no startup block keeps its image bytes, its steps and its peak cells.

## What exists at the baseline

| Area | Baseline |
| --- | --- |
| Accessor clauses | `Parser::accessor_clauses` reads `\| get:` and `\| set:` after a member's `;` into `Declaration::accessors`. `declare_accessors` and `lower_accessors` (`lowering_accessors.inc`) make hidden functions `$get_<field>` and `$set_<field>`, linked by the attributes `accessor.get`, `accessor.set` and `accessor.of`. No member may begin with `\|`, so a clause before the first member is free. |
| Words | `get`, `set`, `value`, `instance` and `scope` are ordinary identifiers that the parser reads by context. `static` and `override` are keywords. `static scope` does not parse: `declaration()` expects `let` after `static`. |
| Startup | Each file has one body, built in `Lowering::run`, of its bindings and top-level statements in source order. `Converter::order_initialization` orders the files, and makes an initializer written as a literal an image constant (`Converter::constant_initializer`). Class statics run on first active use behind the guards of `lower_static_initializers`. A file-level `scope { }` is `E0607`. |
| References | A reference is one handle word. Nil is word 0 and carries nothing: a typed nil cannot be told from an untyped one at run time. An object's class is read from its header (`StateOp::ReadTag`). |
| Summaries | `PurityAnalysis::Fact::Hooks` and `Summary::hooks` exist and nothing sets them. |
| Tier 3 | `analyse_flags` (`tier2.cpp`) proves which frame slots are initialized at each entry of a function program. It assumes that parameters are initialized at entry 0, that every other slot is not, and that a `Call` exit sets only its result slots. |

## Class hook clauses

<!-- example: fragment: 0.3.0 design that the compiler does not accept yet -->
```neura
let Session = class (
    | get: instance;
    | set: {
        if (value == nil) {
            failure.trap("a Session reference is never nil");
        }
        return value;
    };

    export last user: string;
    export let init = func(user: string) => set;
);
```

```text
class-body  = "(" { hook-clause } { member } ")"
hook-clause = "|" ("get" | "set") [ "." "override" ] ":" (expression | block) ";"
```

| Rule | Status |
| --- | --- |
| Hook clauses are written after the `(` of a declared class and before its first member. A `\|` clause after a member's `;` stays a field accessor clause of that member, with today's rules and diagnostics. | proposed |
| A class has at most one `get` and one `set` clause, in either order. Each ends with `;`, including one whose body is a block. A clause takes no tags and no `export`. | proposed |
| Both clauses yield a reference of the class. The body is an expression of the class's type, or a block that returns one on every path or traps. A getter that only reads is written `\| get: instance;`. | proposed |
| In a getter, `instance` is the stored reference being read. In a setter, `value` is the incoming reference and `instance` is the reference the destination held before, or nil when the destination held none, held an object that is not of the class that declares the clause, or is a parameter or a result. Both names are immutable inside the clause. | proposed |
| `get`, `set`, `value` and `override` after the `.` keep their meaning only in a clause. Everywhere else `get`, `set` and `value` stay ordinary identifiers. | proposed |
| A class without a clause inherits each one from the nearest base class that declares it. A class that declares a clause its base already has, declared or inherited, writes `\| get.override:` or `\| set.override:`; without it the clause is `E0539`, and `.override` with nothing to override is `E0540`. A class with no effective clause of a kind has *identity* behavior for that kind. | proposed |
| Interfaces, modules, enums and anonymous classes cannot declare hook clauses (`E0216`). A class with a `#Synchronized` method, or one whose base has one, cannot either (`E0541`): a hook would read the instance outside its guard. | proposed |
| A singleton class may declare hooks. They apply to references to its instance and do not make the class constructible. A generic class may declare hooks; each specialization has its own policy. | proposed |
| The formatter writes each clause on its own line at the indentation of the members, with one blank line between the clauses and the first member. It does not reorder clauses. The language server offers `get` and `set` after a leading `\|`, `instance` and `value` inside a clause, shows the effective clauses and where they come from in a class's hover, and treats a clause as a definition for navigation from an `.override`. | proposed |

## Policies

The *policy* of a reference is the class whose effective clauses apply to it. An object's policy is its own class, read from its header. A nil reference keeps the policy of the event that produced it.

| Rule | Status |
| --- | --- |
| A getter's policy is the class of the object when the stored reference is not nil; otherwise the policy the stored nil carries; otherwise the declared type of the storage when that is a class. | proposed |
| A setter's policy is the first of: the class of the incoming object; the policy an incoming nil carries; the policy of what the destination holds now (its object's class, or its nil's policy); the declared type of the destination when that is a class. | proposed |
| When no rule gives a class, as for an untyped nil stored to an interface or `class` binding that holds nothing, the event is identity and the stored nil carries no policy. | proposed |
| An object of class `B` replaces one of class `A` under `B`'s setter. `A` is not consulted. A rule about what one piece of storage may hold belongs in a field accessor. | proposed |
| A nil produced by a hook, or admitted under a policy, carries that policy. Nil compares equal to nil whatever it carries, hashes as nil, and a type test of nil is false. | proposed |
| A hook's result is nil or an object whose class is exactly the event's policy. Anything else is the trap `class hook result is not a <Policy>`. A clause whose body is `instance`, `value` or `nil` on every path needs no check. A clause of a class that is not `#Final` that returns a `new` expression is `E0543`, since a subclass that inherits it would always trap. | proposed |
| The result must also be assignable to the destination by the ordinary rules. A policy never makes an assignment legal. | proposed |
| A checked cast of a nil that carries policy `P` to a type `P` is not assignable to fails as the cast of a `P` object would. Any other cast of nil gives nil with the same policy. | proposed |
| Nothing is committed until the setter returns. A trap in a setter leaves the destination as it was, and does not undo what the right-hand side or the hook already did. | proposed |

## Events

An *event* is one run of an effective clause. The table gives every source operation that has one. Copying words is never an event.

| Source operation | Event |
| --- | --- |
| Reading a local, parameter, global, field, static field, captured binding, or list or array element of class type | One getter |
| Reading a component of a tuple or a payload of an enum that has class type: a projection, a pattern binding, a destructuring | One getter on the component |
| Initializing or assigning a local, global, field, static field, captured binding, element or component of class type, including `append` and each element of a `new` list in written order | One setter, then the commit of value, policy and initialized state together |
| Passing an argument to a parameter of class type, including each variadic argument and each argument of a call through a function value | One setter in the caller, after that argument is evaluated and before the next written argument is |
| Returning a value of class type | One setter in the callee on the result, after the expression is evaluated and before any exit work of the function |
| Calling a method on a receiver | The receiver expression's own read, and nothing for handing it to the method |
| Reading `instance` in a method, constructor, destructor or accessor body, and `instance` or `value` in a hook clause | None |
| `new` of a class | None for the new object; the binding or argument that receives it has its own event |
| Building, copying, passing, returning or binding a tuple, enum, list or array as a whole; a compiler temporary; growth of a list; the box that carries a value over a one-word boundary | None |
| A field accessor adapter: its `value` parameter, its result, and the call that reaches it | None; `object.field` is one getter on the result, and `object.field = x` is one setter before the adapter runs, with the field's stored reference as the previous value when the field has storage and nil otherwise |
| A debugger stop, an inspection, `neura inspect` | None |

`last copy = identity(session);` therefore has five events: the read of `session`, the admission of the argument, the read of the parameter in the function's `return`, the admission of the result, and the initialization of `copy`.

| Rule | Status |
| --- | --- |
| A read used by `==`, `is`, a cast, text conversion, `destroy`, a capture, a destructuring or a discarded expression statement is a read like any other. | proposed |
| A compound assignment to a property keeps today's order: receiver, getter, right-hand side, computation, setter. | proposed |
| A lambda's captured binding is a binding of the lambda: creating the lambda reads the outer binding (one getter) and initializes the copy (one setter), and each read in the body is one getter. | proposed |
| An extension function's receiver is a written parameter and is admitted like one. This row waits for extension sets, which belong to the calls milestone. | proposed |
| A constructor's own-field stores bypass field accessors as today and keep their class hook events. | proposed |
| A read of storage that is not initialized, or of a destroyed object, fails as today before any getter runs. | proposed |
| A declaration of class type whose type has an effective setter, and that has no initializer, is `E0545`: a local, a global, an instance field or a static field. A deferred `last` binding is exempt, since it has no default. `= nil` is an initializer and is admitted at run time. | proposed |
| `RepeatCollection` of a class type admits its value once per element, in index order. | proposed |

### Recursion

| Rule | Status |
| --- | --- |
| Each task has a stack of active events. An entry is the kind (get or set), the policy, and the identity of the reference: the object's handle, or nil. Handles are never reused, so no entry can name a released object. | proposed |
| Starting an event equal to an active one is the trap `class hook recursion: <get or set> of <Policy>`. A hook may freely read or admit other objects, and references of other classes. | proposed |
| The stack holds 64 entries. A 65th is the trap `class hook nesting limit exceeded`. | proposed |
| A hook body may not use the scheduler: a channel operation, a guard, `spawn`, `scope`, `select`, or a call whose summary says it may (`E0544`). An event therefore starts and ends without its task being suspended. | proposed |
| Both traps are NVM trap reasons with their own names in `--trace` and in the debugger's stop reason. | proposed |

## Representation

| Rule | Status |
| --- | --- |
| A class, interface or `class` type is *policy-carrying* in a linked, specialized program when some class assignable to it has a clause that is not identity. A program with no hook clause has no such type. | proposed |
| A reference of a policy-carrying type is two value words: the handle, then a `uint32` policy word. The policy word is 0 when the handle is not nil or the nil carries no policy, and otherwise the class's layout identifier plus 1. No bit of the handle is used. | proposed |
| The two words are stored wherever a tuple's words are: frame slots, parameters, results, fields, globals, and aggregate elements. A shape's size counts both, and `E0449` names the policy words when they take a shape over 64. | proposed |
| A receiver is one word, since it is never nil. Converting between a policy-carrying type and one that is not adds a zero policy word or drops the word. | proposed |
| Over a boundary that carries one word (a channel message, a task's result, a parallel item, a callback, an object constant), the two words are boxed and unboxed as an aggregate is. The box is not an event. | proposed |
| A function without a body (a host binding, a kernel, a foreign function) cannot take or return a policy-carrying reference (`E0546`). Its other side has no policy word and could not keep one. | proposed |
| The collector treats the policy word as the `uint32` it is. Equality and identity hashing read the handle only. | proposed |

## Lowering and artifacts

| Stage | Change | Status |
| --- | --- | --- |
| Parser | `class_hook_clauses` beside `accessor_clauses`, called after the `(` of a declared class; the result is `Declaration::hooks`, a list of `HookClause` (kind, override, spans, expression or block). `skip_accessor_clauses` recovers past a bad clause. | proposed |
| Lowering | A new `lowering_hooks.inc`. Each clause becomes a hidden static function `$class_get(instance, policy)` or `$class_set(value, instance, policy)` of its class, with the attributes `hook.get` and `hook.set` on the class and `hook.of` on the function (identifiers 12 to 14 in `attributes.def`). The checks `E0538` to `E0543` and `E0545` run here. | proposed |
| NeuraIR | A module that declares a clause is written with the next free NeuraIR minor version when the change lands, so that an older reader refuses it. The encoding gains nothing else. Events are not stored: they are derived from the tree. | proposed |
| Elaboration | A new pass, `elaborate_class_hooks` (`src/neura-ir/class_hooks.cpp`), runs on the linked program after `specialize` and before `optimize`, at every place those two are called together (`build_image_bytes`, `emit_ir`, the test runner and `tooling/debug.cpp`). It computes the policy-carrying types and wraps each event's operand in `IrOp::HookRead` or `IrOp::HookAdmit`. It does nothing when the program has no clause. | proposed |
| Derivation | The pass applies the event table to the tree. A `Load`, `Field`, `Index`, `Project` or `EnumProject` of class type is a read; an `Initialize`, `Store`, `Append`, element of `Collection`, call operand or `Return` operand is an admission. A symbol whose name begins with `$` is a compiler temporary and has no event, except a capture field `$capture<N>`. The parameters and result of a function that carries `accessor.of` or `hook.of`, and `IrOp::Receiver`, have none. Anything the rules do not exempt is an event, so a temporary that lowering forgets to name with `$` shows as an extra event in the trace tests and never as a missing one. | proposed |
| Stored form | `HookRead` and `HookAdmit` exist only after elaboration. The `.nir` writer refuses them, the verifier accepts them only in an elaborated program, and there it derives the events again and requires each to be wrapped exactly once. `emit-ir` prints them. | proposed |
| Summaries | Elaboration sets `Summary::hooks`, with the clause as its cause, on every function that contains an event with a clause that is not identity, and the clause's own effects join that function's summary. `#Pure` and the other checked tags are checked again after elaboration; a violation is reported with today's code and a label at the clause. | proposed |
| Literal nil | An initializer the pass wraps in `HookAdmit` is not a literal, so `constant_initializer` no longer makes it an image constant: it becomes a startup action at its place in source order. The converter decides nothing about hooks. | proposed |
| Converter | `HookRead` and `HookAdmit` become: select the policy, compare it against the classes of the operand's static type that have a clause, in ascending layout identifier, and call the clause's function, or do nothing for identity. A setter's result goes to fresh slots, and one exit's `assignments` then writes handle, policy word and flags to the destination. | proposed |
| NVR | The next free NVR minor version: the state operations `HookEnter(kind, policy, handle)` and `HookLeave`, which the hidden functions run first and last, and the trap reasons `HookRecursion` and `HookLimit`. The result check is a `ReadTag` and a compare. The writer emits the version only for an image with a clause. | proposed |
| Decompiler | Reads and writes the two operations, shows a policy word by its class name when the image carries names, and reports a hidden hook function as inferred structure only from its `HookEnter`. | proposed |

### Optimization and tiers

A hook is a call with effects, whatever its body does. A getter whose body is `instance` is still an event that a debugger can stop in and a trace counts.

| Pass | Rule | Status |
| --- | --- | --- |
| IR `eliminate-common-subexpressions` (`repeatable`) | A `HookRead` or `HookAdmit`, and a call of a function whose summary has the hook fact, is never repeatable, and ends the availability of every expression before it. | proposed |
| IR `hoist-loop-invariants` (`pure_and_total`) | Neither node is pure or total. | proposed |
| IR `remove-pure-discards`, `remove-dead-stores` | A discarded event stays. A store that is an admission stays even when the stored value is never read. | proposed |
| IR `inline-calls` (`inlinable`) | A function that contains an event, and a hook function, is not inlined. | proposed |
| IR `propagate-copies`, `fold-constants` | A read through a `HookRead` is not a copy of the storage, and a `HookAdmit` of a literal nil is not a constant. | proposed |
| Tier 2 | A block that contains `HookEnter` or `HookLeave` is declined and runs at tier 1. A block that calls a hook function is an ordinary block with a `Call` exit. | proposed |
| Tier 3 inlining (`inline_calls`) | A function with `HookEnter` or `HookLeave` is not inlined at first, as a callee with a `Helper` is not. Native inlining follows in its own change, and then keeps both operations and the result check in place. | proposed |
| Tier 3 value numbering (`number_values`, `Memory::apply`) and LICM (`hoist_invariants`) | The call of a hook function is a `Call`: `is_effect`, never numbered or hoisted, and it kills every memory class. `HookEnter` and `HookLeave` are added to `is_effect` and are never hoisted. | proposed |
| Tier 3 flags (`analyse_flags`) | Arguments are admitted in the caller, so "parameters are initialized at entry 0" holds. A destination is written only by the commit exit after the setter's call returns, so across that call its flag is still 0, and the handle word and policy word of one reference always change in the same exit. The entry that resumes after the call is analysed with the destination not initialized. A hook-bearing global is never an image constant, so no flag is proven for it from the image. | proposed |
| Checks | `NEURA_TIER3_VERIFY=1` and `NEURA_TIER_CHECK=2` run over every hook test program, and each pass above has a program in which it must leave the operation in place. | proposed |

### Debugger

| Rule | Status |
| --- | --- |
| Hook clauses have source maps. Step-in from a statement with an event enters the clause; step-over runs it; a breakpoint in a clause stops there. | proposed |
| Stopping, stepping and inspecting run no clause, call neither `HookEnter` nor `HookLeave`, and change no step count. Variables show the stored handle and, for a nil with a policy, `nil (policy Session)`. | proposed |
| A test runs one program with and without breakpoints and inspection at every stop and compares output, steps, peak cells and a counter each clause increments. | proposed |

## Startup blocks

<!-- example: fragment: 0.3.0 design that the compiler does not accept yet -->
```neura
static scope {
    last handlers = load_handlers();
    register_all(handlers);
}
```

```text
startup-block = "static" "scope" block
```

| Rule | Status |
| --- | --- |
| `static scope { ... }` is written among the top-level statements of a file or among the members of a module. It has no `export`, no tags and no `;` after its `}`. | proposed |
| `scope` after `static` is read by context. `static let` and every other use of `static` and `scope` are unchanged. | proposed |
| The block is an ordinary block: its bindings are local to it and it may shadow nothing an ordinary block could not. It is not a task scope and is not evaluated by the compiler. | proposed |
| It runs once per execution, at its place in source order among its file's initializers and top-level statements, under [the existing order](../lang/Modules.md#initialization). A module's block runs where the module is written. A file may have several. | proposed |
| A block runs whether or not anything in its file or module is used, and is never removed as unreachable. A module shared by several imports, from source or from `.nir`, runs its blocks once. A module that inherits another does not run the base's blocks again. | proposed |
| It is an error in a function, a class, an interface, an enum, a nested block, or a generic declaration (`E0218`). | proposed |
| `return` in the block is `E0470`. So is postfix `?` on a `Result`, once the absence milestone adds it. A fallible helper's result is handled in the block. | proposed |
| The block is not inside a function: `scope { }`, `spawn` and `select` written in it are rejected as they are at file level today (`E0607`, `E0603`, `E0606`). Calling a function that uses them is legal where it is legal from a top-level statement. | proposed |
| A trap in a block ends the run before `main`. Each test of `neura test` runs every block again in a fresh execution. `check`, `emit-ir`, the language server and test discovery run none. A library with startup blocks and no entry point is not runnable. | proposed |
| Class statics keep first-active-use initialization. A block that uses a class initializes it there. | proposed |
| Lowering adds the block to its file's body as an `IrOp::Block` with its own span, in `Lowering::run`. No NeuraIR or NVR version changes. | proposed |
| A debugger launch sets its breakpoints before the first initializer runs, so a breakpoint in a startup block stops. | proposed |
| The formatter writes `static scope {` on one line and the body as a block. The language server lists a startup block in the outline as `static scope` and folds it. | proposed |

### Hook-bearing initializers

| Rule | Status |
| --- | --- |
| A global, module member or static field of class type whose initializer is admitted by a clause is initialized at run time, at its place in source order or on its class's first active use, even when the initializer is written `nil`. | proposed |
| Until then the storage is not initialized. A read of it is the existing failure for an uninitialized global or field, including a read during a class's own initialization. | proposed |
| Loading an image, starting a worker ahead of use and caching an image run no clause. | proposed |

## Diagnostics

The milestone shares its assigned codes with the lifetime milestone (`defer`, `using`, generators and events; `docs/design/Lifetime030.md` on its own branch), which uses `E0219` to `E0222`, `E0471` to `E0479` and `E0550`. `E0223`, `E0224`, `E0312` to `E0314`, `E0547` to `E0549` and `E0551` to `E0559` are assigned to neither and stay free.

| Code | Title | Span | Message and fix |
| --- | --- | --- | --- |
| `E0215` | malformed class hook clause | the first wrong token | "expected 'get' or 'set' after '\|'", "expected ':'", "a class hook clause ends with ';'", "only '.override' may follow 'get' or 'set'", or "a class hook clause takes no tags". Help: `\| get: <expression>;` or `\| set: { ... };` |
| `E0216` | class hook outside a declared class | the `\|` | "an interface (module, enum, anonymous class) cannot declare class hooks". Help: declare the hooks on a named class |
| `E0217` | malformed startup block | the first wrong token | "expected '{' after 'static scope'", "a startup block takes no tags (no 'export')", or "a startup block has no ';' after its '}'", with a fix that removes the `;` |
| `E0218` | misplaced startup block | `static scope` | "a startup block is written at file or module level". Help: move it, or call a function from a file-level block |
| `E0470` | `return` in a startup block | the `return` or the `?` | "a startup block returns nothing and cannot be left early". Help: handle the result with `match`, or put the work in a function |
| `E0538` | duplicate class hook clause | the second clause, with a label at the first | "class 'C' already has a 'get' hook" |
| `E0539` | class hook replaces an inherited one without `.override` | the clause keyword, with a label at the inherited clause | "class 'D' inherits a 'set' hook from 'C'". Fix: insert `.override` |
| `E0540` | `.override` on a class hook that overrides nothing | `.override` | "no base of 'C' has a 'get' hook". Fix: remove `.override` |
| `E0541` | class hook on a class with synchronized methods | the clause, with a label at the `#Synchronized` | "a hook would read a 'C' outside its guard". Help: validate in the synchronized methods |
| `E0542` | class hook that does not yield its class | the expression, or the end of a block that can fall through | "a 'get' hook of 'C' yields a 'C', found 'T'" or "this path returns no value" |
| `E0543` | class hook that returns a new object | the `new` expression | "a subclass of 'C' would inherit this hook and receive a 'C'". Help: mark the class `#Final`, or return `instance`, `value`, `nil` or an existing object of the same class |
| `E0544` | class hook that may suspend | the clause, with a label at the operation or call | "a class hook cannot use the scheduler: 'send' may suspend" |
| `E0545` | hooked class storage without an initializer | the declaration's name | "'C' has a 'set' hook, so 'slot' needs an initializer". Fix: append `= nil`, marked as changing behavior, since the hook then runs |
| `E0546` | hooked class reference at a bodyless function | the parameter or result type | "'C' references carry a class policy that a host binding cannot keep". Help: pass an identifier or a plain data class |

Each code gets its page under `docs/diagnostics/`, a line in the index, and a failing example. Existing codes are reused where they already say the right thing: an immutable binding assigned (`value = other;` in a clause), an accessor clause on a member that cannot have one, and a tag's own code for a checked tag that a hook breaks.

## Edge cases

Accepted unless marked *rejected*.

**Hook clauses, parser**

1. `class ( | get: instance; export let x: int = 0; )`: one getter, identity setter.
2. `| set: { return value; }; | get: instance;`: either order.
3. A comment or a blank line between `(` and the first `|`, between clauses, and between `|` and `get`: accepted, and kept by the formatter.
4. `| get : instance ;` with spaces, and a clause split over lines after `:`.
5. `| get.override: instance;`, and `| get . override : instance;`.
6. `| get: instance` with no `;`: rejected, `E0215`, at the next token.
7. `| got: instance;`: rejected, `E0215`.
8. `| get.final: instance;`: rejected, `E0215`.
9. `#Deprecated` on the line above a clause: rejected, `E0215`; the tag is not moved to the class.
10. `export | get: instance;`: rejected, `E0215`.
11. Two `get` clauses: rejected, `E0538`.
12. `class ( export let x: int = 0; | get: instance; )`: the clause is an accessor clause of `x`, so `instance` is not an `int`: rejected with today's accessor diagnostics, and a note that class hooks come before the first member.
13. `class ( export let run = func() { }; | get: instance; )`: rejected with today's `E0516`-family diagnostic and the same note.
14. `interface ( | get: instance; ... )`, `module ( | get: ... )`, a clause in an anonymous class body: rejected, `E0216`.
15. `class<T> ( | get: instance; ... )`: accepted, one policy per specialization.
16. `class ( | get: instance; )` with no members: accepted.
17. `class ( | )` and `class ( | get: `, as in an unfinished edit: `E0215`, the class still gets its symbol, and the members after the next `;` are parsed.
18. A local, field or parameter named `get`, `set` or `value` elsewhere in the class: accepted, unchanged.
19. A block getter with no `return` on one path: rejected, `E0542`.
20. `| set: value;` as an expression: accepted.

**Hook clauses, semantics**

21. `let a: Session = nil;` where `Session` rejects nil: accepted by the compiler; the setter traps at run time, at startup for a global.
22. `let a: Session;`: rejected, `E0545`. `last a: Session;` assigned later: accepted, one setter at the assignment.
23. `let c: class = nil;` then `c = session; c = nil;`: the first is identity, the second runs `Session`'s setter with the object, the third runs it with nil and the object as `instance`.
24. An `Admin` object assigned to a binding that holds a `Session` object: `Admin`'s setter only.
25. A derived class with no clause: its references use the base's clauses with the derived policy, and a base getter that returns another base object traps with `class hook result is not a Guest` for a derived class `Guest`.
26. A derived class that redeclares `get` without `.override`: rejected, `E0539`. `.override` in a class whose bases have none: rejected, `E0540`.
27. `| get: new Session("x");` in a class that is not `#Final`: rejected, `E0543`. In a `#Final` class: accepted.
28. A getter that maps nil to a shared object it reads from a static field of the same class that holds an object: accepted; the nested read is another object's event.
29. The same getter when the static field holds nil: trap, `class hook recursion: get of Session`.
30. A setter that stores `value` into a field of the class's own type: trap, recursion, since the store is a set of the same object.
31. A hook that reads a reference of an unrelated hooked class: accepted, that class's event runs.
32. A hook that sends on a channel, or calls a function that may: rejected, `E0544`.
33. A hook on a class with a `#Synchronized` method: rejected, `E0541`.
34. `value = other;` in a setter: rejected with the existing immutable-binding code.
35. `session.user` on a nil that a getter passes through: the existing nil failure, after the getter.
36. `session == nil` where the getter maps nil to an object: false; the comparison reads through the getter.
37. `session is Session` on a nil that carries the policy: false.
38. A nil with policy `Admin` in a `Session` binding cast with `as Guest`: fails as the cast of an `Admin` would.
39. `[session, other]` then `pair.0`: no event for the tuple, one getter at the projection.
40. `match (holder) { Some(found) => ... }`: one getter on the payload and one setter for `found`.
41. `new([first, second])` of a hooked class: two getters and two setters, alternating in written order.
42. `call(a, b)` where `a`'s setter traps: `b` is not evaluated.
43. A function value called with a hooked argument: admitted by the caller with the function type's parameter type.
44. `return session;` in a function with a `scope`: the result is admitted before the scope's exit.
45. A method call on a receiver: one getter for the receiver expression, none for `instance` inside.
46. `init(user: Session) => set` with a `Session` field: per parameter, a getter on the parameter and a setter on the field, in parameter order.
47. `f(a, b: Session)`: grouped parameters are two parameters, admitted in order.
48. A hooked reference sent on a channel or returned from a task: boxed, no event for the box, and the receive binding has its setter.
49. A hooked class as a parameter of a host binding: rejected, `E0546`.
50. A tuple of 33 hooked references: 66 words, rejected, `E0449`, naming the policy words.
51. A hooked class imported from a `.nir` built before the importer adds a hooked subclass: accepted; events are derived on the linked program, so the library needs no rebuild.
52. A `#Pure` function that reads a reference whose getter writes a counter: rejected after elaboration with `#Pure`'s existing code, labelled at the clause.
53. A discarded `session;` statement: one getter, kept by the optimizer.
54. The same getter read twice in one expression, `session.user + session.user`: two events in every mode.
55. A loop that reads an unchanging hooked local: one event per iteration; nothing is hoisted.

**Startup blocks**

56. `static scope { }` empty, at file level and in a module: accepted.
57. Two blocks in one file, with globals between them: run in source order with the globals.
58. `static scope { ... };`: rejected, `E0217`, with the fix that removes `;`.
59. `static scope` followed by a line break and `{`, or by a comment and `{`: accepted; the formatter joins them.
60. `static scope (handle) { }`, `static scope;`, `static scope` at the end of the file: rejected, `E0217`; recovery skips to the matching `}` or the next `;`.
61. `export static scope { }` and `#Tag` above a block: rejected, `E0217`.
62. `static scope { }` in a function body, a class, an interface, a nested block, or a `module<T>`: rejected, `E0218`.
63. `static let scope = 1;` in a class, a local named `scope`, and the statement `scope { }` in a function: unchanged.
64. `return;` in a block: rejected, `E0470`. `return` inside a lambda written in the block: accepted, it returns from the lambda.
65. `break` or `continue` outside a loop in the block: today's error; inside a loop in the block: accepted.
66. `scope { }` or `spawn` directly in the block: rejected with today's `E0607` and `E0603`.
67. A binding in the block named like a file global: follows the block scoping of an ordinary block.
68. A block in an imported file whose members are never used: runs.
69. A file imported by two files, and a `.nir` module linked twice: its blocks run once.
70. Files that import each other: blocks run in the cycle's order, and a read of a global not yet initialized is the existing failure.
71. A block that traps: `main` does not run, and the exit status is the trap's.
72. A block in a library built with no entry point: `check` accepts it; `build` of an image still needs an entry.
73. `static scope {` with no closing brace: `E0203` as today, and the editor still lists the file's earlier symbols.
74. A hooked global `let current: Session = nil;` read by a block written above it: rejected by the existing use-before-declaration rule; read through a function called from that block: the uninitialized-global failure.

## Order of work

Each step is one commit on `m4-integration` with the targeted tests named. The full gate runs once, at the end.

| Step | Content | Targeted tests | Changes images |
| --- | --- | --- | --- |
| 1 | This note | doc checks | no |
| 2 | `static scope`: parser, lowering into the file body, `E0217`, `E0218`, `E0470`, formatter, outline, pages and examples | parser and UI fixtures for cases 56 to 74; a multi-file order program; `neura test` reset; a debug-adapter test with a breakpoint in a block | only programs that use it |
| 3 | Hook clause syntax and declarations: parser, `lowering_hooks.inc`, attributes, the NeuraIR minor, `E0215`, `E0216`, `E0538` to `E0545`, formatter and language server. A program with a clause is rejected at the end of lowering with "class hooks are not executable yet", so nothing unimplemented can run. | parser and UI fixtures for cases 1 to 34; `.nir` round trip; an older-minor reader refusal | no |
| 4 | The audit of lowering's temporaries (every hidden symbol named with `$`), `elaborate_class_hooks`, the derivation, the verifier rule, `Summary::hooks`, the IR optimizer rows, `E0546` | IR tests of the event count for each row of the event table; one must-decline test per IR pass | no |
| 5 | The NVR minor, `HookEnter` and `HookLeave` in the machine, loader, writer, assembler and decompiler, the trap reasons, tier 2 and tier 3 declines | assembler-level tests; a malformed-image test for each operation | no |
| 6 | The converter: two-word references, selection and dispatch, the commit exit, boxing at one-word boundaries, hook-bearing initializers; the step 3 rejection is removed; debugger display | the trace programs below in every execution mode, with `NEURA_TIER3_VERIFY=1` and `NEURA_TIER_CHECK=2`; the debugger zero-event test | programs with a clause |
| 7 | Language pages, the diagnostics pages, examples, changelog, status | doc checks, doc examples | no |
| 8 | Native tier 3 for hook functions, when the performance gate asks for it | tier-boundary and the hot-read benchmark | no |

## Gate and acceptance

- Every image of the identity set, the extended set and the examples is byte for byte the image built before the milestone, with the same steps and peak cells. No program there declares a hook or a startup block.
- A trace corpus with hand-derived expected output, in which each clause appends to a log: locals, fields, static fields, parameters and results, arrays and lists, tuples and enums, captures, destructuring, patterns, interfaces, base classes, `class`, generic specializations and variadics; typed and untyped nil; replacing an object of one class with another; inheritance and `.override`; a setter that traps and leaves the old value; `last`; a destroyed owner; initialization cycles. Bounded `any` joins the corpus with the matching milestone.
- The corpus gives the same log from source, from `.nir` and from a serialized image, optimized and not, in every execution mode of [the baseline](Release030.md#execution-modes), and with optional metadata stripped.
- Startup: multi-file order, an import cycle, a module linked twice, a trap before `main`, an unused module, a fresh run per test, a class first used in a block, and a debugger stop in a block.
- A hooked global written `= nil` runs its setter at startup and is not in the image's constants; a read before it fails as uninitialized.
- The debugger test of the debugger table passes with zero difference.
- The recursion and nesting traps, the result check and the cast rule each have a program, and a malformed image that leaves an event open or closes one twice is rejected by the loader or trapped.
- The performance gate of the baseline holds, and a benchmark of a hot hooked read is added to the recorded set with its steps.

## Migration

No existing source changes meaning: no class has a hook, and `static scope` did not parse. For the migration guide:

- A class that gains a `set` hook makes every declaration of its type without an initializer an error (`E0545`); the guide shows `= nil` and what then runs at startup.
- A global or static of a hooked class written `= nil` is no longer set when the image loads. Code that read it early, during a class's own initialization or from an earlier file, now fails as uninitialized.
- A top-level statement that existed only to run startup work may move into `static scope`, which also gives it local bindings.
- Prebuilt `.nir` needs no rebuild for hooks. A module that declares one cannot be read by a 0.2 compiler.
- The count of fields and statics whose default a hook would admit ([the baseline's inventory](Release030.md#migration-inventory)) is the number of declarations a class would have to give initializers to if it adopted a `set` hook.

## Decisions taken without the owner

| Decision | Reason |
| --- | --- |
| Policy provenance is a second value word, present only for policy-carrying types | It reuses the value words of the storage milestone, uses no handle bits, and leaves programs without hooks unchanged. |
| Events are derived on the linked program and not stored in `.nir` | Whether a type has a hook depends on every class in the program, and a library must not need a rebuild when an importer adds a hooked subclass. |
| A compiler temporary is recognized by its `$` name | It changes no `.nir` bytes of existing programs, and a missed temporary shows as an extra event. |
| Reading `instance` in a method is not an event | The receiver was read where the call was written; an event per member access would make a class whose getter can yield nil unusable. |
| A captured binding is initialized and read with events | A capture is a binding of the lambda, and bindings have events. |
| Building or binding a whole tuple or enum has no event; projecting a component has one | Copying an aggregate must never validate again what it holds; only a source projection or binding does. |
| The recursion key is kind, policy and object identity, with nil as one identity per policy | It is deterministic and uses no address. It traps exactly the cases that would otherwise not terminate, and is stricter for nil. |
| The active-event stack holds 64 entries | A fixed bound, equal to the inline word limit, that no hand-written program reaches. |
| A hook cannot use the scheduler (`E0544`) | The guard then never has to survive a suspension; the stricter choice until that lifecycle is designed. |
| Hooks are rejected on classes with synchronized methods (`E0541`), on anonymous classes (`E0216`) and in bodyless signatures (`E0546`) | Each would bypass a guard, break structural identity, or lose the policy; rejection is the stricter choice. |
| A clause that returns `new` in a class that is not `#Final` is an error | It is the statically obvious case of a result of the wrong class. |
| Every declaration of a type with a `set` hook needs an initializer, not only static fields | An implicit nil would be an admission the source does not show. |
| The optimizer never removes, merges, hoists or inlines an event, even one whose clause is pure and total | An event is observable to a debugger and a trace; the rule is the stricter one and costs only programs that use hooks. |
| `static scope` is rejected in a generic declaration and takes no tags | A block per specialization would run more than once; tags have no meaning for it yet. |
| `;` after a startup block is an error with a fix | The form has none, and one spelling keeps formatted sources uniform. |
| NeuraIR and NVR take "the next free minor" instead of a number | Other milestones allocate versions at the same time; the format pages record the number when the step lands. |
