# Neura 0.3.0: matching, `any`, optional access and result propagation

This note fixes the design of the 0.3.0 matching milestone before it is implemented, as [the 0.3.0 baseline](Release030.md) requires: `match.is`, guards and nested patterns in `match`, the inline type `any`, optional access `?.`, coalescing `??`, and the postfix `?` that returns an error. It builds on [storage and construction](Storage030.md), whose value words it uses, and on the calls note of the same release (`docs/design/Calls030.md` on the calls milestone's branch), whose argument rules it assumes. Rows move to [Types](../lang/Types.md), [Core language](../lang/CoreLanguage.md) and [Errors](../lang/Errors.md) in the change that implements them.

Status: *proposed* on 2026-11-17, and *implemented* on the milestone branch the same day for steps 2 to 8 and 10 of the [order of work](#order-of-work). The owner was away and had asked for autonomous work toward 0.3.0, so the rows are implemented as written by autonomous agents and await the owner's confirmation; a row marked "implemented, proposed" is in the compiler and still awaits it. Step 7 implements the executable representation. Step 11 derives the selected pattern-binding hook events. The remaining qualification includes the full step 9 mode matrix and the debugger presentation in step 8. Later changes to a row are made here, with the reason, under [Decisions taken in the implementation](#decisions-taken-in-the-implementation).

Only `any` adds to NeuraIR: one type kind and three operations. Nothing is added to NVR, NVM or the tiers: an `any` uses the runtime’s existing word types. A program that uses no feature of this note builds the image it builds today, byte for byte.

A reference read or admission in these rules runs the event defined by the [class-hook contract](Hooks030.md#events).

## What exists at the baseline

Checked against the code of the milestone branch on 2026-11-17.

| Area | Baseline |
| --- | --- |
| `match` | `parse_match` and `match_arm` (`src/parser/parser.cpp`) read a variant name with one level of bindings. A nested pattern is `expected ')'` and a guard is `expected '=>'` ([Types](../lang/Types.md#enums)). `Lowering::match` (`src/neura-ir/lowering.cpp`) requires an enum subject (`E0411`), saves it in `$match`, reads the tag once, and builds a chain of `Branch` or `SelectValue` on it. |
| Type tests | `value is T` is `IrOp::ClassIs` and requires class reference types on both sides (`E0415`). `as` to a class is `IrOp::ClassCast`, after a visible `let.as` conversion is tried. |
| Absence | `nil` for references, `Std.Core.Optional<T>` and `Std.Core.Result<T>` as library enums read with `match`. `a?.b` and `a ?? b` are `E0204`. `?` is only the conditional operator, read in `expression_tail`. |
| Heterogeneous values | `Std.Core.Variadic.Argument`, a library enum that widens integers to 64 bits. There is no `any`. `any` is an ordinary identifier, used as a method name by `Std.Collections.Sequence`. |
| Erasure | A `Task<T>` or `Scope` can be converted to `class`: `let c: class = task;` is accepted, and the result is no longer known to be [scope-bound](../lang/Types.md#scope-bound-types). |
| Value words | A tuple is stored as its value words. Payload enums become value words in a later step of the storage milestone; until then an `Optional` with a payload is a heap object. |
| Floats in tier 3 | An arithmetic NaN keeps its hardware bits until they are observed; a canonicalization is placed at each observation, and verifier rule V9 checks the placement (`src/nvm/tier3_float.cpp`, `tier3_verify.cpp`). |
| Wide exits | Tier 2 declines an exit with more than 255 argument words; the interpreter runs it. |

## `match.is`

<!-- example: fragment: a 0.3.0 design; the form does not compile yet -->
```neura
let describe = func(value: any) -> string {
    return match.is (value) {
        count: int64 => "an integer",
        float64 => "a float",
        shape: Shape if shape.sides > 4 => "a large shape",
        Shape => "a shape",
        nil => "nothing",
        _ => "something else",
    };
};
```

| Rule | Status |
| --- | --- |
| `match.is (subject) { arms }` selects an arm by the type of the subject's value. It is an expression with expression arms or a statement with block arms, as `match` is. | implemented, proposed |
| An arm's pattern is a type, `name: Type`, `nil`, or `_`. A bare type tests and binds nothing. `name: Type` also binds `name`, a constant of that type visible in the guard and the arm. | implemented, proposed |
| The subject is evaluated once, into a hidden snapshot. Every test reads the snapshot. The arms are tried in written order, the first one that matches is selected, and only its result is evaluated. | implemented, proposed |
| The subject's static type is a class, an interface, `class`, `any`, or a scalar or string type. A subject of a type-parameter type is an error; convert it with `as any`. | implemented, proposed |
| A class or interface pattern matches a live reference whose class is that type or derives from or implements it. A scalar or string pattern matches a value of exactly that resolved type: `int` and `int32` are one type, and `int32` never matches an `int64`. No conversion, `let.as` cast, parsing or numeric conversion takes part. | implemented, proposed |
| `nil` matches an absent reference. A type pattern never matches nil. `_` matches everything. | implemented, proposed |
| A binding is initialized from the snapshot by the checked refinement `ClassCast`, never by a source `as`. For a class type this is one admission of the binding; a bare type pattern admits nothing. If an admission hook gives nil, the arm stays selected. A later read of the binding is an ordinary read, and no flow analysis treats it as non-nil. | implemented, proposed |
| A type test is a passive use of the class: it runs no initializer and no hook. | implemented, proposed |
| When the subject is a class, an interface, `class` or `any`, the last arm is an unguarded `_`. No attribute of a class makes a set of class patterns exhaustive. When the subject is a scalar or a string, one unguarded arm of its type, or `_`, is exhaustive. | implemented, proposed |
| A pattern that cannot match the subject's static type is an error. An arm that an earlier unguarded arm already covers is an error: a repeated type, a subtype after its base, any arm after `_`. | implemented, proposed |
| The arms of a `match.is` expression have a common type by the rules of `match`: the expected type, or the first arm's. | implemented, proposed |

## Guards and nested patterns in `match`

<!-- example: fragment: a 0.3.0 design; the form does not compile yet -->
```neura
let Shape = enum (Dot, Line(int), Box { size: [int, int], label: Optional<string> });

let area = func(shape: Shape) -> int {
    return match (shape) {
        Shape.Dot => 0,
        Shape.Line(length) if length < 0 => 0,
        Shape.Line(length) => length,
        Shape.Box { size: [width, height], label: Optional.Some(_) } => width * height,
        Shape.Box { size: [width, _], label: Optional.None } => width,
    };
};
```

| Rule | Status |
| --- | --- |
| An arm is `pattern if condition => result`. The guard is a `bool` expression that sees the arm's bindings. It is evaluated only after the whole pattern has matched and its bindings are made. If it is false the next arm is tried; its effects have happened. | implemented, proposed |
| A payload position holds a sub-pattern: `_`, a name, a variant pattern, or a tuple pattern. A single identifier is always a binding; a dotted name is always a variant. | implemented, proposed |
| A tuple pattern is `[p, q, ...]` with one sub-pattern per element. The subject of a `match` may be a tuple, and then each arm's pattern is a tuple pattern or `_`. | implemented, proposed |
| Type patterns appear only as the whole pattern of a `match.is` arm. Literal, range, list, rest and property patterns, and user-defined deconstruction, are not part of 0.3.0. | implemented, proposed |
| A name is bound at most once in a pattern. Bindings are constants of the arm. | implemented, proposed |
| Within an arm, sub-patterns are handled in written order, depth first: the payload or element is projected, and then tested if the sub-pattern is a variant or tuple pattern, or bound if it is a name. A projection of a class reference is one read, and its binding one admission. An arm that fails at a later sub-pattern has performed the projections and bindings before it. | implemented, proposed |
| Each arm projects for itself. No projection is shared between arms, and the optimizer may merge two only when neither has an effect. | implemented, proposed |
| Coverage is computed over variants and tuples: an enum's constructors are its variants, a tuple has one, and every other type is covered only by `_` or a name. A guarded arm covers nothing. Without `_`, every value must be covered, and the error shows one uncovered value as a pattern. An arm covered by the earlier unguarded arms is an error. | implemented, proposed |
| A `match` whose arms are all in today's form, with one level of bindings and no guard, keeps today's lowering and its NeuraIR. | implemented, proposed |

## `any`

<!-- example: fragment: a 0.3.0 design; the form does not compile yet -->
```neura
let log_all = func(values: ...any) -> int {
    let integers = 0;
    for (let value: any in values) {
        if (value is int64) {
            integers += 1;
        }
    }
    return integers;
};

let count = log_all(7 as int64, "seven", 7.0, nil);
```

| Rule | Status |
| --- | --- |
| `any` is a built-in value type that holds one value of a fixed set of types together with which type it is. It is not dynamic dispatch: the only operations on an `any` are to copy it, to pass, return and store it, `is`, and `match.is`. | implemented, proposed |
| It holds: `bool`; each sized integer; `float16`, `bfloat16`, `float32`, `float64`; `string`; a class or interface reference, including `class`; and nil. | implemented, proposed |
| It does not hold an array, a list, a tuple, an enum (including `Optional` and `Result`), a function value, a primitive reference, a module, or a [scope-bound](../lang/Types.md#scope-bound-types) value. A class that wraps such a value, constructed with `new`, can be held as a reference; the rules of the wrapped value still apply to the field. | implemented, proposed |
| A value is *packed* where an `any` is expected: the initializer of a binding or field of type `any`, an assignment to such a place, an argument for a parameter of type `any`, including a variadic `...any`, a `return` in a function whose result is `any`, and an element of a collection literal whose element type is `any`. `value as any` packs anywhere. | implemented, proposed |
| Nothing else produces an `any`. Inference never solves a type parameter to `any` unless an argument already is one, the first arm of a conditional or a match does not become `any` because a later arm differs, and no operator packs its operands. | implemented, proposed |
| A parameter of type `any` takes no default (the calls note's `E0226`), and an `any` is not an argument of an untyped variadic parameter `...`, which would format it (`E0493`). | implemented, proposed |
| In overload selection, packing ranks below every other match, so `f(x: int32)` is preferred to `f(x: any)` for an `int32`. | implemented, proposed |
| Packing an `any` gives the same `any`. There is no nesting. | implemented, proposed |
| Packing keeps a scalar's exact bits, width and signedness, a string's text, and a reference's identity. It allocates nothing and runs no hook: it moves a value already evaluated. | implemented, proposed |
| A float is packed with its bit pattern. Packing is an observation of the bits: an arithmetic NaN is the canonical NaN in the package in every tier, and a NaN made with `from_bits`, or negated, keeps the bits the language gives it. | implemented, proposed |
| Reading a class reference out of an `any`, by a `match.is` binding, is one read and one admission by that reference's class, as for any class value. Storing a scalar over an `any` that held a reference replaces the reference word with nil. | implemented, proposed |
| There is no `as` out of an `any`, no arithmetic, no member access, no `==`, no `equals`, `hash_code` or `to_string`, and no formatting. Match first. | implemented, proposed |
| An `any` is not [transferable](../lang/Types.md#transferable-types), whatever it holds. It is not the result of a spawned call, a parallel item, a callback argument or result, or a host call's argument, because those boundaries would box it. | implemented, proposed |
| A type-parameter value may be packed. The rule on what `any` holds is checked on the specialized program, at the instantiating call. | implemented, proposed |
| Converting a scope-bound value to `class`, to an interface or to `any` is an error from 0.3.0, so that no later packing can hold one. | implemented, proposed |
| `any` is a type name wherever a type is read. No class, interface, enum, alias, module or type parameter may be named `any`. A function, method, field, parameter or local named `any` keeps its meaning. | implemented, proposed |

### Representation

An `any` has these four words, in this order, wherever a value is stored: a frame slot, a parameter, a result, a field, a global, an element of an aggregate collection, or the payload of an enum.

| Word | NVR type | Content |
| --- | --- | --- |
| tag | `uint32` | Which type the value has, from the table below |
| bits | `uint64` | A scalar's encoding in the low bits, zero above: `bool` as 0 or 1, an integer in two's complement at its own width, a float as its IEEE or `bfloat16` encoding. Zero for a string or a reference. |
| text | text | The string. The empty text for anything else. |
| reference | object reference to the root class layout | The class or interface reference. Nil for anything else, and for nil itself. |

The root reference has one additional `uint32` nil-policy word when the linked program has a policy-carrying root class. Each place holding an `any` uses that same layout.

| Tag | Type | Tag | Type | Tag | Type |
| --- | --- | --- | --- | --- | --- |
| 0 | reference or nil | 5 | `int64` | 10 | `float16` |
| 1 | `bool` | 6 | `uint8` | 11 | `bfloat16` |
| 2 | `int8` | 7 | `uint16` | 12 | `float32` |
| 3 | `int16` | 8 | `uint32` | 13 | `float64` |
| 4 | `int32` | 9 | `uint64` | 14 | `string` |

| Rule | Status |
| --- | --- |
| Each initialized carrier has one NVR type per word. The collector traces its text and reference words. Its integer words are scalar data. | implemented, proposed |
| The tags are fixed numbers, the same in every module, so linking remaps nothing. The class of a reference is the object's own layout, which the linker already remaps. | implemented, proposed |
| A bare nil has tag zero, zero scalar bits, empty text, a nil reference and zero policy where present. | implemented, proposed |
| The tag is program data. Execution never needs names or a source map to use it. | implemented, proposed |
| An `any` counts four words, or five with a root reference policy, toward the 64-word inline limit (`E0449`). | implemented, proposed |
| When the root reference carries a policy, its fifth word uses the class-hook milestone's encoding. | implemented, proposed |

## Optional access and coalescing

<!-- example: fragment: a 0.3.0 design; the form does not compile yet -->
```neura
let Owner = class ( export let name: string = ""; );
let Pet = class ( export let owner: Owner = nil; );

let owner_name = func(pet: Pet) -> string {
    return pet?.owner?.name ?? "nobody";
};
```

| Rule | Status |
| --- | --- |
| `receiver?.member` and `receiver?.method(arguments)` evaluate the receiver once. If it is nil the result is `Optional.None`, and the member is not read and the arguments are not evaluated. Otherwise the member is read or the call made, and the result is `Optional.Some(result)`. | implemented, proposed |
| The type is `Optional<M>` for a member or result of type `M`, always, also when `M` is a reference or an `Optional`. A call that returns nothing gives `Optional<Unit>`, `Some(Unit.Value)` after the call. `Some(nil)` and `None` stay different values. | implemented, proposed |
| The receiver is a class or interface reference, or an `Optional` of one. For an `Optional`, `None` skips; `Some(r)` reads `r` once and then skips if it is nil. This is how a chain `a?.b?.c` has the type `Optional<C>`. | implemented, proposed |
| `a?.b.c` applies `.c` to an `Optional` and is an unknown member; there is no chain-wide short circuit. | implemented, proposed |
| `?.` is not an assignment target and takes no `++` or `--`. | implemented, proposed |
| `left ?? right` evaluates `left` once. A nil reference, or `None`, gives `right`, which is evaluated only then. A live reference gives itself; `Some(v)` gives `v`, read once, and is not tested again: `Some(nil) ?? x` is nil. | implemented, proposed |
| `left` is a class or interface reference or an `Optional<T>`. The expression has the type of the reference, or `T`; `right` is typed with that as its expected type and must be assignable to it. There is no numeric conversion and no unwrapping of a second level. | implemented, proposed |
| Neither operator hides a failure. A trap in the receiver, a member getter, an argument or the fallback is that trap. | implemented, proposed |
| Neither operator makes a type non-nil. A later read of the same place is a new read. | implemented, proposed |

## Postfix `?`

<!-- example: fragment: a 0.3.0 design; the form does not compile yet -->
```neura
let parse_pair = func(left: string, right: string) -> result.Result<int> {
    let a = int32.parse(left)?;
    let b = int32.parse(right)?;
    return result.Result<int>.Ok(a + b);
};
```

| Rule | Status |
| --- | --- |
| `operand?` takes a `Std.Core.Result<T>`, the library's own enum or an overlay of it. The operand is evaluated once. `Ok(v)` gives `v`, of type `T`, as one read of the payload. `Err(e)` returns `Result<U>.Err(e)` from the enclosing function, with the same error value, where `Result<U>` is that function's result type. | implemented, proposed |
| The enclosing function is the innermost function or lambda. Its result type is written, or given by the lambda's expected type, and is a `Result`. It is never inferred from a body that contains `?`. | implemented, proposed |
| The return is an ordinary `return`: task scopes are joined, regions released, and the cleanup of the cleanup milestone run, as for a written `return`. | implemented, proposed |
| `Ok(nil)` is success. A trap stays a trap: `?` handles no trap and unwinds nothing. | implemented, proposed |
| `?` is an error where there is no such function: a file or module statement, a global or field initializer, a startup block, a constructor, a destructor, and a function whose result is not a `Result`. | implemented, proposed |
| `?` does not apply to `Optional` or to a user enum. | implemented, proposed |

## Grammar

```text
match-is     := "match" "." "is" "(" expression ")" "{" [ is-arm { "," is-arm } [ "," ] ] "}"
is-arm       := is-pattern [ "if" expression ] "=>" ( expression | block )
is-pattern   := "_" | "nil" | type | identifier ":" type

arm          := pattern [ "if" expression ] "=>" ( expression | block )
pattern      := "_" | variant-pattern | tuple-pattern
sub-pattern  := "_" | identifier | variant-pattern | tuple-pattern
variant-pattern := qualified-name [ "(" sub-pattern { "," sub-pattern } ")"
                                  | "{" field { "," field } "}" ]
field        := identifier [ ":" sub-pattern ]
tuple-pattern := "[" sub-pattern { "," sub-pattern } "]"

postfix      := primary { "." identifier | "?." identifier | "[" expression "]"
                        | arguments | type-arguments arguments } [ "?" ]
coalesce     := or-expression [ "??" coalesce ]
```

| Rule | Status |
| --- | --- |
| `match` directly followed by `.` and `is` begins a `match.is`. `match` followed by `(` is unchanged. | implemented, proposed |
| In an `is-arm`, an identifier directly followed by `:` is a binding and what follows is a type. Otherwise the pattern is a type, read by the type parser, so `Box<int>`, `App.Shape` and `string` are patterns. | implemented, proposed |
| `if` after a pattern begins a guard. The guard ends at `=>`. | implemented, proposed |
| `?.` and `??` are single tokens, added to the lexer's compound symbols. Neither sequence is valid today, so no program changes meaning. | implemented, proposed |
| A `?` token after a postfix expression is decided by the next token alone. If the next token can begin an expression (an identifier, a literal, an interpolated string, `(`, `[`, `{`, `-`, `!`, `~`, `++`, `--`, `new`, `func`, `match`, `import`, `true`, `false`, `nil`), the `?` is the conditional operator's. If the next token is `.`, it is an error. Otherwise (`)`, `]`, `}`, `,`, `;`, `:`, `=>`, `?`, `??`, a binary or assignment operator that is not also a prefix, `as`, `is`, `and`, `or`, the end of the file) it is postfix propagation. | implemented, proposed |
| Every valid conditional today has an expression after its `?`, so none changes. `r? - 1` is read as a conditional and fails for its missing `:` with today's error, which gains the help "to return an error here, write `(r?)`". Member access, a call or an index after a propagated value is written `(r?).name`, `(r?)(x)`, `(r?)[i]`. | implemented, proposed |
| `r?.name` is always optional access. On a `Result` it is an error whose fix is `(r?).name`. | implemented, proposed |
| `r??` is the token `??`. Two propagations are written `r? ?` or `(r?)?`. | implemented, proposed |
| Precedence: `?.` and postfix `?` are at the level of member access. `??` is a new level, looser than `\|\|` and tighter than `..` and `? :`, and associates to the right. `a ?? b ? c : d` is `(a ?? b) ? c : d`. | implemented, proposed |
| `neura fmt` writes `a?.b` and `r?` without spaces, `a ?? b` with spaces, `pattern if guard => result` with single spaces, and `match.is (x) {` as it writes `match (x) {`. A trailing comma after the last arm is kept or added as for `match`. | implemented, proposed |
| The recovering parser keeps a `match.is` whose last arm is incomplete, an arm without a result, and `a?.` at the end of a buffer, so that completion can offer type names after `match.is` arm starts and member names after `?.`. The language server offers a fix that adds a missing `_` arm, shows the type of a pattern binding, and renames a binding within its arm. | implemented, proposed; not at the very end of a buffer |
| The debugger shows an `any` as its tag's type name and its payload, read from its value words. It runs no getter and no hook. | proposed; debugger presentation pending |

## Lowering and artifacts

| Rule | Status |
| --- | --- |
| The parser gives `MatchArm` (`src/parser/syntax.hpp`) a pattern tree in place of its flat variant and bindings, and an optional guard. `Statement::Kind::Match` gains a flag for `match.is`. `Expr::Kind` gains `OptionalMember`, `Coalesce` and `Propagate`. | implemented, proposed |
| `match.is`, and a `match` with a guard, a nested pattern or a tuple pattern, lower in two phases inside one `ScopedValue` or `Block`. The head saves the subject in `$match`, declares each binding of each arm as a deferred `last` local (NeuraIR 1.10), and sets a hidden `uint32` local `$arm` to the number of arms. Phase one visits the arms in order; each arm is guarded by `$arm` still being unset, performs its projections, tests and stores in written order as nested `Branch` nodes, evaluates its guard, and stores its index in `$arm`. Phase two is today's chain of `Branch` or `SelectValue`, on `$arm`. | implemented, proposed |
| Tests use existing operations: `EnumTag` with a comparison, `EnumProject`, `Project`, `ClassIs`, `ClassCast`, a comparison with `Nil`, and for an `any` the operations below. | implemented, proposed |
| `a?.m` lowers to a `ScopedValue` that saves the receiver, compares it with nil, and selects `EnumConstruct` of `Some` around the member read or call, or of `None`. `a ?? b` saves `a` and selects by a nil comparison or by `EnumTag`, with `EnumProject` for the payload. `r?` saves `r`, branches on `EnumTag` to a `Return` of `EnumConstruct` of `Err` around `EnumProject` of the error, and yields `EnumProject` of the payload. | implemented, proposed |
| The compiler resolves `Std.Core.Optional`, `Std.Core.Unit` and `Std.Core.Result` from the trusted library by path, as it resolves primitive companions (`Lowering::library_module`), so a file needs no import to use the operators. | implemented, proposed |
| NeuraIR gains the type kind `Any` and the operations `AnyPack` (one operand of a holdable type; the result is an `any`), `AnyTag` (an `any`; the result is its `uint32` tag) and `AnyUnpack` (an `any`; the result is the node's type, which names the payload). They use ordinals 66 to 68 in NeuraIR 1.12. Scoped returns also require this minor. | implemented, proposed |
| `value is T` on an `any` lowers to a `ScopedValue` that saves the value in a hidden constant local and yields a `SelectValue` on its tag: for a scalar or string type, the comparison of `AnyTag` with that type's tag; for a class or interface, `ClassIs` of the `AnyUnpack` of the reference when the tag is 0, and `false` otherwise. A `nil` arm is the same shape with a comparison to `Nil`. Every `AnyUnpack` the compiler emits is therefore inside a region selected by a tag test of a constant local. | implemented, proposed |
| The verifier checks that `AnyPack`'s operand type is holdable, and that an `AnyUnpack` reads a constant local inside the selected region of a `Branch` or `SelectValue` whose condition compares that local's `AnyTag` with the tag of the unpacked type. A `.nir` file that breaks either rule is rejected. | implemented, proposed |
| The converter (`src/backend/converter.cpp`) gives `Any` the four value words above through `value_words` and the aggregate layout of elements. `AnyPack` writes the tag constant and routes the operand to its word, a scalar zero-extended from its own encoding; the other words are constants. `AnyTag` reads the tag word. `AnyUnpack` selects the low lanes of the bits word, or the text or reference word. | implemented, proposed |
| The converter routes a float’s lanes into the unsigned bits word. At native tier 3, observing those bits requires the reference encoding under V9. With zero supplied at run time, the hot test ran 1,000 iterations with the canonical float64 NaN under `NEURA_TIER3_VERIFY=1`. The compiled loop placed a float64 `Canon` between its deferred divide and the integer comparison of the packed bits. Exhaustive float16 and bfloat16 payload tests also agree with tier 0, including paths the native tier hands back to the reference executor. | implemented, proposed |
| NVR, NVM, the collector and the tiers do not change, and the NVR version of an image is not raised by `any`. A block the lifter cannot recognize runs at tier 1; native lowering of such a block follows the performance gate. | implemented, proposed |
| An argument of type `any` uses one exit argument per word. A call with more than 255 argument words runs on the interpreter, as today. | implemented, proposed |
| The effect and retention summaries treat `AnyPack` of a reference as a copy of the reference: what retains the `any` retains the object. Projections, guards and skipped operands are effects in the optimizer's analysis: it does not evaluate a guard before its pattern, a skipped member, argument or fallback, or anything after a `?` that returns. | implemented, proposed |
| The decompiler shows the individual typed words of an `any` with the names the image provides. It claims no recovery of `match.is`, `?.`, `??` or `?`, which lower to the branches a programmer could write. | implemented, proposed |

## Diagnostics

The calls milestone and the matching milestone share one allocation. This note uses and reserves the codes below; the calls note has E0225 to E0229, E0315, E0316, E0480 to E0489 and E0560 to E0564.

| Code | Title | Span | Message and fix |
| --- | --- | --- | --- |
| `E0230` | malformed `match.is` | the offending token | A missing subject, a pattern that is neither a type, `name: Type`, `nil` nor `_`, or a variant or tuple pattern in a `match.is`. Help: use `match` for an enum. |
| `E0231` | pattern form that is not supported | the sub-pattern | A literal, a range, a list or rest pattern, a property pattern, or a type pattern inside a `match` pattern. Help: bind the value and test it in a guard. |
| `E0232` | postfix `?` that needs parentheses | the `?` | "`?` is followed by `.`"; fix: `(r?).name`, or `r?.name` for optional access. |
| `E0233` | malformed optional access | the `?.` | `?.` not followed by a member name, or used as the target of an assignment, `++` or `--`. |
| `E0234` to `E0239` | reserved | | |
| `E0317` | reserved | | |
| `E0490` | pattern that cannot match | the pattern | "a value of type Shape is never a string"; lists the subject's type. |
| `E0491` | unreachable match arm | the arm | Labels the earlier arm that covers it. Fix: remove the arm, or move it up. |
| `E0492` | value cannot be held in `any` | the packed expression | "a value of type int32[] cannot be held in any"; help: hold it in a class and pack the reference. On a specialization, also labels the instantiating call. |
| `E0493` | operation is not defined for `any` | the operator or member | "match the value first"; fix for `as T`: a `match.is` arm `name: T`. |
| `E0494` | optional access on a receiver that is never absent | the receiver | "int32 is not a reference or an Optional of one"; fix: `.`. On a `Result`: fix `(r?).name`. |
| `E0495` | invalid operands of `??` | the left operand, or the fallback | The left is not a reference or an `Optional`, or the fallback is not assignable to the expression's type. |
| `E0496` | `?` on a value that is not a `Result` | the operand | Names the type. For an `Optional`: help `to_result(error)?`. |
| `E0497` | `?` outside a function that returns a `Result` | the `?` | Labels the function's result type, or says that the context has none. Help: handle the result with `match`. |
| `E0498` | `any` at a boundary that would box it | the expression or type | A spawned call's result, a parallel item, a callback, a host call. |
| `E0499` | reserved | | |
| `E0565` to `E0569` | reserved | | |

Existing codes keep their cases: a `match.is` on an open type without a final `_`, and uncovered values, are `E0410`; a repeated variant arm and an arm after `_` in today's form stay `E0412`; an invalid subject is `E0411`; a guard that is not `bool` is `E0403`; a name bound twice in a pattern is `E0301`; a type named `any` is `E0311`; `&any` is `E0428`; an `any` given to a task or a channel is `E0602`; a scope-bound value converted to `class`, an interface or `any` is `E0605`; a member applied to an `Optional` is `E0305`.

## Edge cases

Each case is a test. "Accept" gives the result, "reject" the code.

`match.is`:

1. A subject of type `class` with arms `Circle`, `shape: Shape`, `nil`, `_`: accept; a `Circle` takes the first arm.
2. The same without `_`: reject, `E0410`, also when the class is `#Final`.
3. `Shape` before `Circle` where `Circle` extends `Shape`: reject, `E0491` at `Circle`. With a guard on the `Shape` arm: accept.
4. An arm after `_`: reject, `E0491`. `_ if ready => ...` followed by `_`: accept.
5. A nil subject: takes `nil`, or `_` when there is no `nil` arm; never a type arm.
6. A subject `x: int32` with the single arm `v: int32 => v`: accept. With an arm `int64`: reject, `E0490`. With `int` and then `int32`: reject, `E0491`.
7. A subject of type `Shape` with a pattern `string`, or with an unrelated class: reject, `E0490`. With an interface the class does not implement but a subclass could: accept.
8. A pattern of a tuple, array, enum or function type on an `any` subject: reject, `E0490`.
9. `item: Box<int>` and `item: App.Shapes.Circle`: accept.
10. A pattern naming an anonymous class through its alias: accept; it matches that occurrence only.
11. A subject of type `T`: reject, `E0411`, with the help `as any`. Of type `T` bounded by a class: accept.
12. `match.is value { ... }` without parentheses, `match.is () { }`, and `match . is (x)` with spaces: reject `E0230`, reject `E0204`, accept.
13. `match.is (x) { }` with no arm: reject, `E0411`.
14. A trailing comma after the last arm, a comment between arms, an arm on several lines: accept.
15. `v: int64 if v > 0 => 1`: accept; the guard sees `v`.
16. Two arms that bind the same name: accept; each binding is local to its arm.
17. A binding that shadows a local of the function: accept, as a match binding does today; the local is hidden in the arm.
18. An expression `match.is` whose arms have different types and no expected type: reject, `E0403`, at the differing arm.
19. A statement `match.is` with block arms, and a `return` inside one: accept.
20. A subject with an effect, `match.is (next())`: the call runs once, whatever the number of arms.
21. A `let.as` conversion from `Circle` to `int64` and an arm `int64`: reject, `E0490`; no conversion takes part.
22. `match.is` on an `any` holding `int8` -128 with arms `uint8`, `int8`: takes `int8`.
23. An incomplete `match.is (x) { item: ` in an editor buffer: the recovering parser keeps the statement; completion offers types.

Guards and nested patterns:

24. `Shape.Line(length) if length < 0 => 0` then `Shape.Line(length) => length`: accept.
25. Only guarded arms for a variant: reject, `E0410`, showing the variant.
26. `Tree.Node(Tree.Leaf(a), b)` with a nested variant: accept; `Tree.Node(Tree.Leaf(a), _)` alone: reject, `E0410`, showing `Tree.Node(Tree.Node(_, _), _)`.
27. `Shape.Box { size: [width, height], label }`: accept; the shorthand `label` binds the field.
28. A tuple subject `match ([a, b]) { [Light.Red, _] => 1, [_, Light.Red] => 2, _ => 3 }`: accept. Without the last arm: reject, `E0410`.
29. A tuple pattern with the wrong number of elements: reject, `E0426`.
30. `Shape.Line(1)`, `Shape.Line(0 .. 9)`, `[first, ...rest]`: reject, `E0231`.
31. `Shape.Line(n: int)` in a `match`: reject, `E0231`.
32. `Shape.Line(length, length)` and `[a, a]`: reject, `E0301`.
33. `Optional.Some(Optional.Some(value))` on an `Optional<Optional<int>>`: accept.
34. A second identical unguarded arm: reject, `E0412` in today's form, `E0491` when nested.
35. `[_, _]` before `[Light.Red, _]`: reject, `E0491`.
36. A guard that is not `bool`: reject, `E0403`. A guard with a call that has an effect, then a false result: the effect happens and the next arm is tried.
37. A guard `if` with no condition, `Shape.Dot if => 0`: reject, `E0204`.
38. A match whose arms are all in today's form: accept, with today's NeuraIR and image.
39. A nested pattern in a match statement with block arms, and bindings used after a `break` out of an enclosing loop: accept.
40. An unqualified name as a whole pattern, `Dot => 0`: reject, `E0412`, as today.
41. A nested pattern over a payload of a class type, `Shape.Tagged(owner)`: accept; one read of the payload and one admission of `owner` when the arm reaches it, and none in arms that fail before it.

`any`:

42. `let v: any = 7;`: accept, tag `int32`. `let v: any = 7 as int64;`: tag `int64`.
43. `let v = 7 as any;` and `let w: any = v;`: accept; one package, not nested.
44. Every edge value: `int8` -128, `int64` minimum, `uint64` maximum, `-0.0`, infinities, subnormals, the canonical NaN, a NaN with a payload made by `from_bits`, the negated canonical NaN, and every `float16` and `bfloat16` bit pattern: packed, matched and read back with equal bits, in every execution mode.
45. `0.0 / 0.0` computed in a hot loop and packed: the package holds the canonical NaN in every mode.
46. A string, the empty string, and a 1 MiB string: accept; the same text, no copy.
47. A class reference, an interface reference, a `class` value, and nil: accept; `===` identity of the reference read back.
48. `let v: any = [1, 2];`, a list, a tuple, `Optional<int>.Some(1)`, a fieldless enum value, a function value, a `&int`: reject, `E0492`.
49. `let c: class = task;` and `let v: any = task;` for a `Task<int>`: reject, `E0605`.
50. A `Wrapper` class holding a list, packed as a reference: accept.
51. `v + 1`, `v.name`, `v as int64`, `v == nil`, `v.to_string()`, `$"{v}"`: reject, `E0493`.
52. `v is int64` and `v is Shape`: accept, a `bool`.
53. `any[]`, `any[list]`, `[any, int]`, `Optional<any>`, a field and a global of type `any`: accept. `&any`: reject, `E0428`.
54. `new any[list]([1, "a", nil])`: accept; three packings in written order.
55. `func(items: ...any)` called with mixed arguments: accept; an untyped `...` parameter keeps `Argument`.
56. Overloads `f(x: int32)` and `f(x: any)` with an `int32`: the first. With a `string`: the second.
57. `identity(7)` with `func<T>(value: T)`: `T` is `int32`, never `any`. `identity<any>(7)`: accept.
58. `let pick = ready ? 1 : "x";`: reject, `E0403`; with `let pick: any = ready ? 1 : "x";`: accept.
59. A generic `func<T>(value: T) -> any => value as any` instantiated with `int[]`: reject, `E0492`, at the instantiating call.
60. `spawn work(v)` with an `any` argument, and a channel of `any`: reject, `E0602`. A spawned function that returns `any`: reject, `E0498`.
61. An `any` holding the only reference to an object across a collection: the object survives. An `any` holding an integer equal to a live handle's bits: it keeps nothing alive.
62. A place of type `any` that held a reference and is assigned an integer: the object becomes collectable.
63. Without a root reference policy, sixteen `any` values occupy 64 words and seventeen report `E0449` at 68. With the policy, twelve occupy 60 words and thirteen report `E0449` at 65.
64. `let any = 1;` and `sequence.any(test)`: accept. `let any = class ( );`: reject, `E0311`.
65. A program with `any` built with names and source maps stripped: the same output.
66. A `.nir` file with an `AnyUnpack` outside a tag test: rejected by the reader's verification.
67. A call with 70 `any` arguments, 280 words: accept, in every execution mode.
68. `func(v: any = nil)` and `func(v: any = 1)`: reject, `E0226`.
69. `console.println("{}", v)` for an `any`: reject, `E0493`.

`?.`, `??` and `?`:

70. `pet?.owner` for a nil and a live `pet`: `None` and `Some(owner)`. For a live pet with a nil owner: `Some(nil)`.
71. `pet?.rename(next())` for a nil `pet`: `None`, and `next` is not called.
72. `pet?.owner?.name`: `Optional<string>`; `None` when the pet or the owner is nil.
73. `pet?.owner.name`: reject, `E0305`, with the help `?.`.
74. `count?.abs()` for an `int32`, and `Module?.member`: reject, `E0494`.
75. `pet?.owner = other;` and `pet?.age++`: reject, `E0233`.
76. `pet?.` at the end of a buffer: the recovering parser keeps the receiver; completion offers members.
77. `pet ?. owner` with spaces around the token: accept. `pet ? .owner`: reject, `E0232`.
78. `pet?.close()` as a statement, where `close` returns nothing: accept; the `Optional<Unit>` is discarded.
79. `name ?? "x"` for `Optional<string>`, and `owner ?? fallback` for references: accept. `count ?? 0` for an `int32`: reject, `E0495`.
80. `Optional<Owner>.Some(nil) ?? fallback`: nil; the fallback is not evaluated.
81. `a ?? b ?? c`: `a ?? (b ?? c)`. `a ?? next()` with `a` present: `next` is not called.
82. `amount ?? 0` for `Optional<int64>`: accept; the literal is `int64`. `amount ?? 0.5`: reject, `E0495`.
83. `nil ?? x`: reject, `E0495`. `v ?? x` for an `any`: reject, `E0495`.
84. `ready || done ?? x`: reject, `E0495`; `??` is looser than `||`.
85. `ok ? a : b`, `ok ? -1 : 2`, `ok ? (a) : [b][0]`, a nested conditional: accept, unchanged.
86. `parse(text)?` in a function that returns a `Result`: accept. In one that returns `int`, in `main`, at file level, in a field initializer, in `init`: reject, `E0497`.
87. `parse(text)? + 1`: accept. `parse(text)? - 1`: reject, with today's error for a missing `:` and the help `(parse(text)?) - 1`.
88. `open(path)?.read()`: reject, `E0494`, with the fix `(open(path)?).read()`.
89. `ok ? first()? : second()?`: accept; each `?` before `:` and `;` is propagation.
90. `nested()? ?` and `(nested()?)?` for a `Result<Result<int>>`: accept. `nested()??`: reject, `E0204`.
91. `lookup(key)?` for an `Optional`: reject, `E0496`.
92. `?` in a lambda whose expected type returns a `Result`: returns from the lambda. In an untyped lambda whose result would be inferred: reject, `E0497`.
93. `?` inside a `scope` block and inside a loop: the tasks are joined and the regions released before the function returns.
94. An `Err` propagated through three functions: the same error value at the top, compared with `===`.
95. `!check()?` and `-total()?`: the propagation applies before the prefix operator.
96. `f(a: load()?, b: next())`: an `Err` from `load` returns before `next` is evaluated.
97. `$"{load()?}"`: accept. `$"{a ?? b}"`: accept.
98. `write(text)?;` as a statement for a `Result<Unit>`: accept; the `#NoDiscard` rule is satisfied.

## Order of work

Each step is one commit on the milestone branch with the tests named. Steps 2 to 5 and 8 add no NeuraIR operation.

| Step | Content | Targeted tests | Changes images |
| --- | --- | --- | --- |
| 1 | This note | doc checks | no |
| 2 | The pattern tree in the parser and `MatchArm`; guards, nested and tuple patterns; the two-phase lowering; coverage; `E0231`, `E0491` | `tests/ui/types`, a match program with expected output, the unchanged NeuraIR of today's matches | no |
| 3 | `match.is` over classes, interfaces, `class`, scalars and strings; `E0230`, `E0490` | `tests/ui/types`, a dispatch program | no |
| 4 | `?.` and `??`: tokens, parser, lowering, `E0233`, `E0494`, `E0495` | `tests/ui/parser`, `tests/ui/types`, a skipped-effects program | no |
| 5 | Postfix `?`: the token rule, lowering, `E0232`, `E0496`, `E0497` | `tests/ui/parser`, an error-propagation program, the conditional regression set | no |
| 6 | `any` in NeuraIR: the type kind, the three operations, the verifier rules, `.nir` reading and writing, packing in lowering, `is` and `match.is` over `any`, `E0492`, `E0493`, `E0498`, the reserved type name, the scope-bound conversion rule with its migration | `nir` tests, `tests/ui/types`, invalid `.nir` fixtures | no, for programs without `any`, apart from the programs the migration rewrites, which it lists with their changed steps and heap cells |
| 7 | `any` in the converter: the four words, fields, elements, parameters and results; the bit-exactness, collector and allocation tests | `conversion` tests, `tier` tests, `tests/backend/programs` with captures in every mode | no, for programs without `any` |
| 8 | Tooling: formatter, completion, the `_` fix, hovers, debugger display of `any` | `tests/tooling` | no |
| 9 | Wide and hot cases: 70 `any` arguments, NaN packed in a hot loop under native tier 3, a generated program for the fuzzer | `tests/backend/tier`, `scripts/program-fuzz.py` seeds | no |
| 10 | Language pages, diagnostics pages, examples, changelog; this note's rows marked implemented | doc checks, `doc-examples` | no |
| 11 | After the class-hook milestone is merged: reads and admissions at the places the rows name, the policy word, and the hook traces | trace programs with hooks, optimized and unoptimized | yes, for programs with hooks and `any` |

## Gate and acceptance

- Every image of the identity set and the extended set is the image the milestone's base builds, byte for byte, except the programs step 6 rewrites for the scope-bound rule, which are listed with their changed steps and heap cells, and `scripts/runtime-identity.py` gives identical captures in every mode of [the baseline's table](Release030.md#execution-modes).
- Exact bits: edge case 44 holds in every execution mode, and source to `.nir` to image gives the bytes of source to image.
- No hidden boxing: a loop that packs, passes, matches and unpacks scalars, strings and references reports the `peak_heap_cells` of the same loop without `any`. Once payload enums are value words, the same holds for `?.`, `??` and `?` over an `Optional` or `Result` whose payload needs no allocation; until then their allocation is that of any payload enum, and the test records which step of the storage milestone the branch has.
- Guarded effects: a program prints one line per projection, guard and arm, and its output equals a hand-written expectation, optimized and unoptimized, at tier 0 and in the default mode.
- Skipped effects: receivers, arguments and fallbacks that must not run print nothing, in the same builds and modes.
- Error propagation: an error keeps its identity through several functions, and the early return joins tasks and releases regions.
- Rejection of erased lifetimes: a scope-bound value cannot reach `class`, an interface or `any` by any sequence of conversions, including through a generic function and a variadic call.
- Identity and provenance: a reference read back from an `any` is the same object with its own class; with hooks, the trace has one read and one admission per binding and none for packing or copying.
- The collector cases of edge cases 61 and 62 pass under a heap limit that forces collection.
- Every row of the diagnostics table has a test with its code, span and message, and every numbered edge case has a test.
- The full suite, the mode matrix, the fuzzer and the benchmark gate run once, at the milestone gate.

## Migration

For the migration guide of the release:

- **A scope-bound value no longer converts to `class` or an interface** (`E0605`). This includes passing a `Task` or `Scope` to a `class` parameter, such as `equals`, and to an untyped variadic parameter. Step 6 counts the sites in `stdlib/`, `examples/` and `tests/` with the compiler and rewrites them; compare handles with `==`.
- **`any` is reserved as a type name.** The repository declares no such type.
- **Nothing else breaks.** `?.`, `??`, postfix `?`, guards and nested patterns were syntax errors, and every conditional keeps its meaning.
- **Standard library:** not rewritten in this milestone, so its images stay identical. The migration milestone may adopt `?` and `??` where the steps and heap cells are shown to be equal or better, and lists each change.
- Pages to update with the implementation: Types (enums and `match`, where the sentences that call nested patterns and guards syntax errors go; type tests; the new section for `any`; scope-bound types; inference and absence), Core language (the operator table, with `?.`, postfix `?` and `??`), Errors (propagation is explicit with `?`, and still never implicit), Collections, Standard library (`Optional`, `Result` and `Variadic.Argument` beside `any`), Tooling, Status, the NeuraIR format page (the type kind and the three operations), Backend (the four words), IR optimization (the effects above), and the diagnostics catalogue.

## Decisions taken without the owner

| Decision | Reason |
| --- | --- |
| An `any` is four typed words (tag, bits, text, reference), not one word per scalar type and not a tagged 64-bit slot. | Every word keeps one NVR type, as value words require, so the collector never guesses; four words is the smallest such shape. |
| Nil in an `any` is tag 0 with a nil reference; there is no separate nil tag. | One representation of absence, equal to the all-zero value. |
| `any` adds no NVR operation and no NVR version. | The converter can route bits between words with existing circuits, so every tier and old runtime sees only what it knows. |
| An `any` has no `==`, `equals`, `hash_code`, `to_string` or formatting. | Each would need a rule for mixed types; matching first makes the type explicit. |
| An `any` is rejected at the boundaries that box a multi-word value, including a task's result. | The alternative is a box the program did not ask for. |
| A scope-bound value cannot be converted to `class` or an interface at all. | Tracking the fact through erasure would need a second kind of reference; rejecting the erasing conversion is the stricter of the two permitted choices. |
| A subject of a bare type-parameter type is rejected in `match.is`. | Its domain is only known after specialization; `as any` states the intent. |
| `match.is` on a class, interface or `any` always needs `_`, also for a `#Final` class. | Exhaustiveness over classes is left for a later release; one rule now. |
| Type patterns are not nested in `match` patterns. | It keeps enum coverage separate from open type tests in the first version. |
| A single identifier in a pattern is a binding and a dotted name a variant. | Variants are written qualified today, so no lookup decides the grammar. |
| Coverage of nested patterns is computed, with an uncovered value shown; a guarded arm covers nothing. | A required `_` on every nested match would hide missing variants. |
| Within an arm, projections and bindings interleave in written order, and a failed arm keeps what it did. | It is the order a reader sees, and it needs no rule about which bindings are delayed. |
| A postfix `?` is decided by the next token alone; `? .` is an error and `r?.name` is optional access. | The parser needs no types and no whitespace rule, and every existing conditional is unchanged. |
| `??` is looser than `\|\|` and right-associative, and its type is the left operand's payload type. | The fallback is then typed by context, with no rule for joining two types. |
| `?.` always gives an `Optional`, and a chain needs `?.` at each step. | No member's type changes with its position in a chain. |
| `?` applies to the library's `Result` only, and never infers a lambda's result type. | The error type is then always `Error`, and the return target is known before the body is typed. |
| The general match lowering uses deferred locals and a selected-arm index. | It needs no new NeuraIR operation and evaluates each projection once. |

## Decisions taken in the implementation

| Decision | Reason |
| --- | --- |
| The postfix `?` lowers to a scoped value that returns from inside it, so NeuraIR 1.12 lets a `return` leave a scoped value; `break` and `continue` still cannot. A `.nir` file with such a return is 1.12. | The lowering this note gives needs it. The converter treats a scoped value's statements as ordinary statements, so it needed no change; the verifier rule and the version are the whole change. |
| The NeuraIR minor of this milestone is one named constant, `nir_any_minor`, 12 on this branch. Minor 11 is the class-hook milestone's. | The release fixes the final numbers when the milestones merge. |
| An arm in the form of 0.2 keeps the flat fields of `MatchArm` beside the pattern tree, which every arm has. | Today's lowering, its NeuraIR and its errors stay byte for byte, and the walkers that only scope names stay correct. |
| An unqualified name as a whole pattern is E0412 in both lowerings, with the qualified spelling as help. | The baseline gave E0304 there, not the E0412 that edge case 40 states; one code for one mistake. |
| A collection literal matched by tuple patterns is a tuple. | `match ([a, b])` has no expected type, and a list literal is not a tuple pattern's subject. |
| E0498 is given for a spawned call's result. A parallel item, a callback and a host call with an `any` keep the diagnostics of those boundaries, which already reject a type they cannot carry; an `any` given to a task or a channel is E0602. | The boundaries' own checks reject `any`, as they reject any type they do not carry. |
| A tuple of more than 64 value words with `any` is checked where tuples are, when the storage milestone enforces E0449. | The limit's driver is the storage milestone's. |
| The step 6 migration of the scope-bound rule found no site in `stdlib/`, `examples/` or `tests/`: no source converts a `Task` or `Scope` to `class`, an interface or `any`. | Counted with the compiler: every file that checked before checks after. |
| A `?` in a loop condition, on the right of `&&`, or in a `for` loop's step returns from there; the verifier's rule that a loop latch transfers no control does not look into operands, and the programs run alike in every build route and tier. | It is the rule as written; `tests/matching/programs/propagation-positions.nra` covers each position. |
| A function value passed to a generic function that packs its parameter is E0492 at the instantiation: the specializer recognizes the object layout a function value has become. | Function values become objects before the check runs. |
