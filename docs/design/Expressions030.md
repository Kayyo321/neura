# Neura 0.3.0: negation words, narrowing, `typeof`, character literals, string chains and return forms

This note fixes the grammar, typing, lowering and diagnostics of the 0.3.0 expression features before they are implemented, as [the 0.3.0 baseline](Release030.md) requires: the `not` keyword and `x is not T`, narrowing bindings in `if` conditions, the `typeof(...)` expression and the `Std.Reflect.Core.TypeInfo` record it returns, character literals, inclusive integer ranges with `..=`, string chains with `..` and appending with `..=`, value inequality `!==`, and the return forms `return if` and `return ... unless`. It builds on [matching](Matching030.md) (`match.is` and its checked refinement), [calls](Calls030.md) (`nameof`, which `typeof` follows) and [records](Records030.md). The rows move to [Core language](../lang/CoreLanguage.md), [Types](../lang/Types.md), [Numbers](../lang/Numbers.md), [Collections](../lang/Collections.md), [the NeuraIR format](../NirFormat.md) and [the NVR format](../runtime/NVR.md) in the change that implements them.

Status: *proposed* on 2026-11-17, to be implemented by autonomous agents the same day; every row awaits the owner's confirmation. The owner was away and asked for autonomous work toward 0.3.0. A row marked "implemented, proposed" is in the compiler and still awaits that confirmation. Changes to a row made during implementation are listed, with the reason, under [Decisions taken in the implementation](#decisions-taken-in-the-implementation).

Only two of the features add operations. A string chain is one NeuraIR operation, `concat text` (op 73, NeuraIR 1.15), emitted as one NVR 1.9 state operation (op 35). `typeof` is one NeuraIR operation, `type of` (op 74, NeuraIR 1.15), that the compiler expands after specialization into existing operations, so it never reaches an image. Everything else lowers to operations that exist today: an image of a program that uses none of the features is byte-identical to the image built before them, and `not x`, `x is not T`, `a !== b`, a narrowing `if`, a character literal, `return if` and `return ... unless` build the same image as the hand-written form the tables below name.

## Summary of the forms

<!-- example: fragment: the 0.3.0 expression forms; these do not compile before the change that implements them -->
```neura
let console = import:Std.Io.Console;

let Shape = class ( export let sides: int = 0; );
let Circle = class : Shape ( export let radius: int = 1; );

let describe = func(value: Shape) -> string {
    if (value is circle: Circle && circle.radius > 0) {
        return "circle of radius " .. circle.radius.to_string();
    }
    return "none" unless value != nil;
    return if (value.sides > 4) -> "polygon"
           else -> typeof(value).name;
};

let shout = func(text: string) -> string {
    let result = text;
    result ..= "!";
    return result;
};

let digits = 0 ..= 9;                     // [0, 1, ..., 9]
let letter: byte = 'H';
let tab = '\t';
let same = not (("a" .. "b") !== "ab");
```

## Grammar

```text
unary          = ( "!" | "not" | "-" | "~" | "++" | "--" ) unary | postfix [ "++" | "--" ]
type-test      = cast-operand ( "as" type | "is" [ "not" ] type )
narrowing      = cast-operand "is" [ "not" ] identifier ":" type     ; only where "Narrowing" allows it
if-condition   = conjunct { "&&" conjunct }                         ; the whole condition of an `if`
conjunct       = narrowing | expression-above-&&
primary        = ... | "typeof" "(" ( expression | type-path ) ")" | char-literal
char-literal   = "'" ( char-ascii | char-escape ) "'"
char-ascii     = any byte 0x20..0x7E except "'" and "\"
char-escape    = "\" ( "n" | "r" | "t" | "0" | "\" | "'" | "\"" ) | "\x" hex hex | "\u{" hex [hex [hex [hex [hex [hex]]]]] "}"
equality-op    = "==" | "!=" | "===" | "!=="
range-or-chain = coalescing [ ( ".." | "..=" ) coalescing [ ":" coalescing ] ]   ; a range
               | coalescing ".." coalescing { ".." coalescing }                  ; a chain
concat-assign  = place "..=" expression ";"                        ; statement level only
return-stmt    = "return" [ return-value [ "unless" expression ] ] ";"
return-value   = expression | return-if
return-if      = "if" "(" expression ")" "->" expression [ ";" ]
                 { "else" "if" "(" expression ")" "->" expression [ ";" ] }
                 "else" "->" expression
```

The parser builds one syntax for both rows of `range-or-chain`: a range node with two or more operands, an inclusive flag and an optional step. Lowering chooses the role from the static operand types (see [String chains](#string-chains)). The operands of `..` and `..=` are parsed as range bounds are today: every binary operator binds more tightly, and `??` binds more tightly and associates to the right. A parenthesized operand is one operand even if its body is itself a chain.

## Operator levels

The table replaces the list in [Expressions](../lang/CoreLanguage.md#expressions). Rows marked new are the additions; every existing level keeps its meaning.

| Level (tightest first) | Operators | Associativity | Status |
| --- | --- | --- | --- |
| 1 | member access, `?.`, indexing, calls, postfix `?`; the primaries `nameof(...)` and `typeof(...)` (new) | left | proposed |
| 2 | postfix `++`, `--` | none | existing |
| 3 | prefix `++`, `--`, `!`, `not` (new), `-`, `~` | right (prefix) | implemented, proposed |
| 4 | `as`, `is`, `is not` (new) | left | implemented, proposed |
| 5–11 | `*` `/` `%`; `+` `-`; `<<` `>>`; `&`; `^`; `|`; `<` `<=` `>` `>=` | left | existing |
| 12 | `==`, `!=`, `===`, `!==` (new) | left; `==` and `!=` also form comparison chains | implemented, proposed |
| 13 | `&&` | left | existing |
| 14 | `||` | left | existing |
| 15 | `??` | right | existing |
| 16 | `..` (range or chain), `..=` (inclusive range, new), optional `: step` | a chain is flat and n-ary; a range takes exactly two bounds | implemented, proposed |
| 17 | `? :` | right | existing |
| statement | `=`, compound assignments, `..=` after a place (new) | not an expression | implemented, proposed |

`not` is `!` spelled as a word, so it binds as tightly as `!`: `not a && b` is `(not a) && b`, and `not x == y` is `(not x) == y`. Write `not (x == y)` for the other reading.

## Negation words

| Rule | Detail | Status |
| --- | --- | --- |
| `not` is a reserved word and a prefix operator. | `not e` is `!e` in every position, with the same operand rules (a `bool`), the same diagnostics and the same image. `!` keeps working. | implemented, proposed |
| `x is not T` is one form. | It is `!(x is T)`, with the type rules of `is`, and builds the same image. `not` directly after `is` belongs to the test; `x is (not T)` is not a type. `x is not not T` is rejected at the second `not` as a type was expected. | implemented, proposed |
| Spelling is kept. | Syntax records which spelling was written, so the formatter, the LSP and diagnostics show `not` where `not` was written. A diagnostic about the operand of `not` names `not`. | implemented, proposed |

## Narrowing

`if (x is y: T) { ... }` tests `x` as `x is T` does and, where the test succeeded, binds `y`: a constant of type `T` made by the checked refinement of `x`, exactly as the arm `y: T` of a `match.is` binds it. It is never a source `as`, so no user conversion runs, and hook admissions and traps are those of `match.is`.

| Rule | Detail | Status |
| --- | --- | --- |
| Where a narrowing may appear. | Only as the whole condition of an `if` (an `else if` included), or as a top-level conjunct of an `&&` chain that is the whole condition. Anywhere else, including inside parentheses, under `!` or `not`, as an operand of `||`, `?:`, `==` or a call, and in the conditions of `while`, `do` and `for`, the token sequence `is name :` keeps its existing meaning (a type test followed by a colon), so `c ? x is T : z`, `f(a: x is T)` and a range step keep their meaning. When that existing reading fails because `:` follows `is name` in an `if`, `while`, `do` or `for` condition, the error is E0265, which names the allowed positions. | implemented, proposed |
| Subject and evaluation. | The subject is the expression before `is`. It is evaluated once, when its conjunct is reached, into a compiler temporary; the test and the binding read that temporary. Conjuncts are evaluated left to right and stop at the first false one, as `&&` does today. | implemented, proposed |
| Subject types. | Those of `match.is`: a class or interface reference, `class`, `any`, a scalar or a string. Others are E0411, a type the subject can never have is E0490, a primitive reference is E0490, and a scope-bound subject that would be erased is E0605, all with their `match.is` texts. | implemented, proposed |
| Scope of `y` after `is`. | `y` is visible in the conjuncts after its own and in the true branch. It is not visible in the `else` branch, in an `else if` chain, or after the `if`. | implemented, proposed |
| Scope of `y` after `is not`. | `if (x is not y: T) A else B` binds `y` in `B` only. When `A` cannot complete normally (it ends every path with `return`, `break`, `continue` or a trap, as the compiler's existing reachability rule decides), `y` is also bound for the rest of the enclosing block, after the `if`, as a local declared at the `if`. An `is not` narrowing is allowed only as the whole condition; inside an `&&` chain it is E0266, because the false branch of the chain does not prove the test failed. | implemented, proposed |
| Several narrowings. | `if (a is p: P && b is q: Q && p.fits(q))` binds `p` from the first conjunct on and `q` from the second on; all are visible in the true branch. | implemented, proposed |
| Names. | The binding is a constant (assigning it is the existing constant-assignment error). It lives in a scope that encloses the later conjuncts and the true branch, so it may hide an outer name, including the subject: `if (x is x: Circle)` makes `x` the refined constant inside the branch. A duplicate of a name declared in the same scope is the existing duplicate-local error; for the after-`if` binding of `is not` that scope is the enclosing block. | implemented, proposed |
| Reassignment of the subject. | The binding is a snapshot. Assigning to `x` inside the branch does not change `y`, and `y` does not change `x`. | implemented, proposed |

### Lowering

A narrowing `if` lowers to the steps `general_match` builds for `match.is`: a constant temporary for the subject, an arm-index local, the class test (`ClassIs`), the binding (`ClassCast` of the temporary, or the scalar or `any` unpacking of `any_pattern`), and a branch on the arm index. `if (x is y: T && rest) A else B`, with one narrowing as the first conjunct, builds the same image as `match.is (x) { y: T if rest => A, _ => B }` written as a statement, and `if (x is y: T) A else B` the same as `match.is (x) { y: T => A, _ => B }`. Conjuncts before a narrowing enclose its steps as branches; after the conjuncts, the arm index selects the true or false branch, so neither branch is duplicated. `if (x is not y: T) A else B` lowers as `match.is (x) { y: T => B, _ => A }`; when `A` cannot complete normally, the binding is declared in the enclosing block and stored on the path that continues after the `if`.

## typeof

`typeof(e)` evaluates `e` once and returns a `Std.Reflect.Core.TypeInfo` describing the type of its value; `typeof(T)`, where the operand is a type, describes `T` and evaluates nothing.

| Rule | Detail | Status |
| --- | --- | --- |
| Result type. | The record `TypeInfo` of the module `Std.Reflect.Core.TypeInfo` (declared in `stdlib/std/reflect/core/type-info.nra`, and written `TypeInfo.TypeInfo` after `let TypeInfo = import:Std.Reflect.Core.TypeInfo;`, as `Optional` is), with the components `kind: Std.Reflect.Type.TypeKind`, `name: string` and `qualified_name: string`. It is never nil. Its generated `equals`, `hash_code` and `to_string` give value equality: `typeof(a) === typeof(b)` compares types, and `==` compares identity as for every record. The library needs no import for `typeof`: the compiler finds the module by path, as it finds `Optional`. | proposed |
| Static or dynamic. | The **dynamic** type of the value for a class or interface reference, `class`, and `any`; the static type for every other type, where the two are the same. For a reference the result describes the most-derived class of the object, so `typeof(shape)` of a `Circle` held as `Shape` describes `Circle`. For an `any` it describes the type of the value held. | proposed |
| Nil. | A nil reference, or an `any` holding nil, gives the `TypeInfo` of kind `Nil`, name `nil`. `typeof` never traps on nil. | proposed |
| Generics. | Generic code is specialized before `typeof` is expanded, so `typeof(value)` with `value: T` describes the type argument of that specialization, such as `int32` or `Box<string>`. | proposed |
| Value types. | Scalars, `string`, tuples, enums (including `Optional<T>`), arrays, lists and function types are described statically: `typeof(Optional.Some(3))` is `Optional<int32>`, kind `Enum`. A record or class held inline by the storage rules of [storage](Storage030.md) is still a class and is described dynamically. | proposed |
| Type operands. | When the operand is a name, a dotted path or an applied generic that resolves to a type (a built-in type name such as `int32`, a class, interface, record, enum or type parameter), `typeof` describes that type statically, with no evaluation. A name that resolves to a value is an expression. | proposed |
| Names. | `name` is the declared name with its type arguments (`Box<int32>`); `qualified_name` is the name the image uses for class text, the module path followed by the name (`App.Shapes.Circle`). Built-in types have equal `name` and `qualified_name` (`int32`, `string`, `int32[]`, `int32[list]`, `(int32, string)`). | proposed |
| Execution data. | The kind and both names are constants of the program. They are in the image, stay when `NAME` and `SMAP` are stripped, and are the same through `.nir`. | proposed |
| Reserved word. | `typeof` is reserved; `typeof` without `(` is E0267. | proposed |
| Operands that have no type. | A `void` call, a module, an overload group or a function name without a call is E0267. | proposed |

`Std.Reflect.Type.TypeKind`, declared in `stdlib/std/reflect/type/type-kind.nra` where the `Std.Reflect` hierarchy places it, has the variants `Nil`, `Bool`, `Integer`, `Float`, `String`, `Class`, `Interface`, `Record`, `Enum`, `Tuple`, `Array`, `List`, `Function`, `Any`, `TypeParameter` and `Other`; `typeof` of a value never gives `Interface`, `Any` or `TypeParameter`, which only type operands give. The record is the extension point of `Std.Reflect`: a later change can add components, `TypeId`, layout and the other packages of `Std.Reflect` without changing what `typeof` returns. Hash and text are those of a record, so they use no identity number and are the same in every build.

### Lowering

Lowering emits `type of` (NeuraIR op 74) with the operand, or with no operand and the type for a type operand, and the result type `(int32, string, string)`; it then calls the module function `Std.Reflect.Core.TypeInfo.from_parts` on that tuple. After specialization, on the linked program, a pass replaces each `type of` with existing operations. The implementation chooses one of two expansions and records the choice under [Decisions taken in the implementation](#decisions-taken-in-the-implementation): a compiler-made method on the root class that each class answers with its own constants through ordinary dispatch, as the default `to_string` answers with its own qualified name, if that changes no image of a program that does not use `typeof`; or, otherwise, a synthesized helper function per static type, called from each site, that holds a nil test, then a chain of `ClassIs` tests over the classes of the program assignable to the static type, most-derived first (deeper classes before their bases, so the first true test is the exact class), each selecting a tuple of constants; for an `any`, the existing tag test chooses between the constants of each scalar and string type and the same class chain on the unpacked reference. Classes the program never constructs may be left out of a chain. With the helper, the cost of one `typeof` of a reference is one test per candidate class; with dispatch it is one call. Either way nothing is allocated other than the record.

`.nir` written before specialization keeps op 74; reading it back and building gives the same image as building from source. A later runtime operation that reads a class identifier directly could replace the chain without changing the result.

## Character literals

| Rule | Detail | Status |
| --- | --- | --- |
| Form. | A single character between single quotes: one printable ASCII byte (0x20 to 0x7E) other than `'` and `\`, or one escape. | implemented, proposed |
| Escapes. | `\n` (10), `\r` (13), `\t` (9), `\0` (0), `\\`, `\'`, `\"`, `\xHH` with exactly two hexadecimal digits, which gives any byte 0x00 to 0xFF, and `\u{H}` whose value is at most FF, which gives that byte. A `\u{...}` above FF is E0260; see [Escapes](#escapes). | implemented, proposed |
| Value and type. | The literal is an integer literal whose value is the byte. Where an integer type is expected it takes that type, as a decimal literal does, and a value the type cannot hold is the existing out-of-range literal error (`let s: int8 = '\xFF';`). Without an expected type its type is `byte` (`uint8`), not `int32`. `'A' + 1` with no context is a `byte` of 66. | implemented, proposed |
| Bytes that are not one character. | A character outside ASCII (any UTF-8 sequence of two or more bytes, such as `'é'` or `'€'`) is E0260, whose help points to `Std.Core.Character` for Unicode code points and to a string literal for text. | implemented, proposed |
| Malformed literals. | `''`, more than one character (`'ab'`), a line end or the end of the file before the closing quote, a raw tab or control byte, and an unknown escape are E0261, with the span of the literal (or of the escape) and, for `'ab'`, a suggestion to use a string. | implemented, proposed |
| Typographic quotes. | `‘` and `’` stay E0101; the note now says that a character literal uses ASCII `'`. | implemented, proposed |

A character literal folds like any integer literal and adds no NeuraIR or NVR operation: `let c: byte = 'H';` builds the same image as `let c: byte = 72;`.

## Escapes

The owner approved on 2026-11-17 adding the Unicode escape, `\0` and `\'` to string literals, and then asked that every escape a character literal accepts also be accepted in strings. The lexer decodes them; no NeuraIR or NVR change follows, and a program that uses none of the new escapes builds the same image.

| Rule | Detail | Status |
| --- | --- | --- |
| Escapes of every literal. | `\n`, `\r`, `\t`, `\0` (U+0000), `\\`, `\'`, `\"`, `\xHH` with exactly two hexadecimal digits, and `\u{H}` with one to six hexadecimal digits in either case, in string, interpolated-string (outside holes) and character literals. | implemented, proposed |
| `\u` in a string. | One Unicode scalar value, encoded as UTF-8. A surrogate (`\u{D800}` to `\u{DFFF}`) or a value above `\u{10FFFF}` is E0269 at the escape; a `\u` without `{`, empty braces, more than six digits, a non-hexadecimal character, or no `}` is E0268 at the escape. | implemented, proposed |
| `\u` in a character literal. | A value up to FF is that byte, as `'A'` is a byte: `'\u{41}'` is 65 and `'\u{E9}'` 233. A larger scalar value is E0260, whose help names `Std.Core.Character` with the value (`Character.of(0x1F600)`). E0268 and E0269 apply as in strings. | implemented, proposed |
| `\x` in a string. | One byte. The decoded bytes of the literal (its characters and escapes together) must be valid UTF-8: `"\xC3\xA9" == "é"`. A literal that is not is E0270 at the literal, labelled at the `\x` escape where the first invalid sequence starts, with the suggestion `\u{HH}` for that byte's code point. In an interpolated string each text part between holes is checked. | implemented, proposed |
| Other escapes. | Unknown escapes and a `\x` without two hexadecimal digits stay E0104 in strings and E0261 in character literals; a raw null byte in a string stays E0104. | implemented, proposed |
| Formatting. | The token keeps the literal as written, so the formatter writes every escape back unchanged. | implemented, proposed |

## Ranges with `..=`

`start ..= stop` is the inclusive integer range: it contains `stop` when the step reaches it. It has the contextual typing, optional `: step`, brace grouping, evaluation order and failure rules of `start .. stop` in [ranges](../lang/Collections.md#ranges). `{0 ..= 5}` is `[0, 1, 2, 3, 4, 5]`, `5 ..= 0 : -2` is `[5, 3, 1]`, `0 ..= 6 : 3` is `[0, 3, 6]`, and `3 ..= 2` is empty.

| Rule | Detail | Status |
| --- | --- | --- |
| Counting. | The element count is computed from the bound ranks as for `..`, with three differences: the range is nonempty when `low <= high` (so `3 ..= 3` has one element), the last offset is `high - low` instead of `high - low - 1`, and the quotient of the last offset by the stride is limited to the `int32` maximum before one is added, so that the count of a range over a whole type's domain (`0 ..= 4294967295` as `uint32`, `int32` or `int64` minimum to maximum) never overflows the distance type and fails with the length failure of an over-long `..` range, never with an arithmetic overflow trap. A range that ends at the type's largest or smallest value (`250 ..= 255` as `uint8`, `int64` maximum) never computes `stop + 1`; construction does not advance past the last element. | implemented, proposed |
| `??`. | `a ..= b ?? c` is `a ..= (b ?? c)`, as for `..`. | implemented, proposed |
| Shape. | Exactly two bounds and an optional step. `a ..= b .. c`, `a .. b ..= c` and a string operand are E0263. | implemented, proposed |
| Statement position. | See [`..=` as a statement](#-as-a-statement). | implemented, proposed |

Inclusive ranges lower to the operations of the existing range lowering, so they add no operation.

## String chains

At level 16, a range node whose first operand has static type `string` is a string chain. Lowering decides the role this way: lower the first operand with the expected type when it is `string`, otherwise without one; a `string` result makes a chain; an integer result makes a range, which keeps its contextual typing (an expected collection type still types integer literal bounds).

| Rule | Detail | Status |
| --- | --- | --- |
| Operands. | Two or more operands, each of static type `string`. Nothing converts implicitly: no `to_string`, no formatting, no overload. A non-string operand is E1100 at that operand, naming its type and suggesting `(operand).to_string()` (or `.to_string()` directly on a name). A string first operand followed by an integer, or an integer first operand followed by a string, is E1100 at the first operand whose type differs. | implemented, proposed |
| Shape. | No step: `"a" .. "b" : 2` is E0263 at the colon. `..=` is never part of a chain: `"a" ..= "b"` in an expression is E0263, whose help names the statement `place ..= text;`. A third integer bound, `1 .. 2 .. 3`, is E0263 at the second operator. | implemented, proposed |
| Evaluation. | Operands are evaluated once each, left to right. If one traps, later operands are not evaluated and nothing is allocated. | implemented, proposed |
| Result. | The UTF-8 bytes of the operands in written order, with empty operands and embedded zero bytes kept and no normalization. The result of `a .. b` is the text of `a + b`. | implemented, proposed |
| Allocation. | After every operand is evaluated, the total byte length is accumulated with an overflow check against the `int32` text length limit; an excess is the existing invalid-text length trap. Then **one** text backing of exactly that length is allocated, charged to the handle and heap budgets as `ConstructText` is, and each operand's bytes are copied once into their final position. A chain of *k* operands totalling *B* bytes does O(*k* + *B*) work, one allocation and *B* copied bytes; it builds no prefix and no byte collection. A failed allocation keeps the effects of the operands and stores nothing. | implemented, proposed |
| Parentheses. | A parenthesized inner chain is its own chain with its own allocation, completed (or failed) before the next outer operand is evaluated: `(a .. b) .. c()` allocates `a .. b`, then calls `c()`, then allocates the result. | implemented, proposed |
| Literal folding. | Adjacent string literal operands fold at compile time: `"hi " .. "there!"` is the constant `"hi there!"` and allocates nothing, and `"[" .. "x" .. name .. "]"` is a chain of `"[x"`, `name`, `"]"`. Folding never crosses a non-literal operand or a parenthesis that encloses one. | implemented, proposed |
| Precedence. | Every binary operator binds more tightly: `a .. b == c` is `a .. (b == c)`, which is E1100 at the `bool`; write `(a .. b) == c`. A conditional binds more loosely: `c ? a .. b : d` takes `a .. b` as its middle arm, and `prefix .. (ready ? "ok" : "busy")` needs its parentheses. `a .. b || c` is rejected, as a range bound today is; parenthesize. | implemented, proposed |
| `+`. | `+` and `+=` on strings are unchanged, including their intermediate allocations. | implemented, proposed |

### `..=` as a statement

A statement that is exactly `place ..= expression;`, with nothing else at the top of the statement, is a compound concatenation assignment when `place` has type `string`. Anywhere else `..=` is the inclusive range above.

| Rule | Detail | Status |
| --- | --- | --- |
| Meaning. | `s ..= t;` stores `s .. t` into `s`. When the right side is an unparenthesized chain, its operands join the chain after the old value: `s ..= name .. "!";` is one chain of three operands and one allocation. A parenthesized right side is one operand. | implemented, proposed |
| Evaluation. | As every compound assignment: the destination's receiver and index are evaluated once, the old value is read once (a getter runs once), then the right operands are evaluated in order, then the chain is materialized, then the value is stored (a setter runs once). A trap in any of these skips the store; earlier effects remain. | implemented, proposed |
| Destination. | A readable and writable `string` binding, field, element or property. A read-only destination has the existing assignment diagnostics. An integer destination, `n ..= 5;`, is E0262, which explains both forms: an inclusive range is an expression (`let r = n ..= 5;`), and `..=` as a statement appends text to a string. Any other destination type, and a stepped right side (`s ..= a : 2;`), is E0262. | implemented, proposed |
| Right operands. | Each must be a `string` (E1100). | implemented, proposed |

### Lowering and artifacts

A chain of two or more operands after folding lowers to `concat text` (NeuraIR op 73, NeuraIR 1.15): ordered `string` operands, a `string` result. It may allocate and trap, so it is not pure, CSE does not merge two of them, and no pass reassociates, splits or combines chains; a parenthesized inner chain is a separate node. A chain that folds to one literal is a string constant. The converter emits NVR state operation 35, `concat-text`, with immediate zero, two or more text arguments and one text result, in NVR 1.9. The loader validates the version, the argument count (at least two), every argument type (text) and the result slot with the existing bounded readers. The assembler and decompiler use the mnemonic `concat-text`. Tier 1 runs it as one state transition; tiers 2 and 3 run it through the existing state-operation helper fallback. Its runtime reads the lengths from the validated handles, checks the total, reserves one backing, roots every operand through the existing collection-safe path, reacquires payload views after any collection, copies each operand's bytes into place, and publishes the handle only on success. A test-only counter in the heap records text allocations and payload bytes copied by the operation so a test can prove one allocation and *B* bytes; it does not appear in `--stats`, `--profile` or any other output. An image that contains no op 35 keeps its existing NVR minor and bytes.

## Value inequality

`a !== b` is `!(a === b)`: the same operands, the same `equals` selection and dispatch, the same diagnostics, and the same image. `!==` is one token (the lexer takes it before `!=`), at level 12. Like `===`, it does not form a comparison chain: `x !== a and b` is the syntax error that `x === a and b` gives (see [Decisions taken in the implementation](#decisions-taken-in-the-implementation)). Extending chains to `===` and `!==` is left for a later change. Status: implemented, proposed.

## Return forms

| Rule | Detail | Status |
| --- | --- | --- |
| `return if`. | `return if (c1) -> v1; else if (c2) -> v2; else -> v3;` evaluates the conditions in order until one is true, then evaluates only the selected value and returns it. Each condition is a `bool`; each value must be assignable to the function's result type, as a `return` value is. The final `else` is required. The `;` after a non-final arm is optional, so `return if (c) -> a else -> b;` is the same statement. It builds the same image as `if (c1) { return v1; } else if (c2) { return v2; } else { return v3; }`. | proposed |
| `unless`. | `return v unless g;` evaluates the `bool` guard first and once; when it is true the statement does nothing, otherwise `v` is evaluated and returned. It builds the same image as `if (!(g)) { return v; }`. The guard applies to the whole return value, including a `return if` or a `return match`. A bare `return;` takes no guard: `return unless g;` keeps its existing meaning, the return of a binding named `unless` followed by a parse error, and is not a guard. | proposed |
| `unless` is contextual. | It is a guard only directly after a complete return value. A binding or function named `unless` keeps its meaning: `return unless;`, `return unless(x);` and `return unless[i];` return or use it, and `return unless unless flag;` uses it as the value and `flag` as the guard. | proposed |
| Definite return. | A guarded return can fall through, so it does not end a non-`void` function; a `return if` with its final `else` always returns. | proposed |
| Block arms. | An arm written as a block that yields its value, `else { ...; yield v; }`, needs the arm-local `yield` and scope exits of the lifetime milestone (M8, `Lifetime030.md` on its branch), which is not merged into this branch. Block arms are specified there and remain *pending*; a `{` after `)` or `else` in a `return if` is E0264 with a note saying so. | pending |

## Tooling

The lexer produces a keyword token for `not` and `typeof`, a symbol token for `!==` and `..=`, and an integer-literal token marked as a character literal for `'c'`, so the formatter writes it back as written. The formatter keeps `not` and `!` as written, puts one space after `not` and around `..`, `..=` and `!==`, writes `x is y: T` with no space before the colon and one after, and writes a `return if` with one arm per line when it does not fit on one line. Formatting twice gives the same text. The LSP highlights `not`, `typeof` and `unless` (as a keyword only where it is a guard), reports the narrowing binding as a constant with its refined type in hover and completion inside its scope, and shows `TypeInfo` as the type of `typeof(...)`. The VS Code grammar in [`packaging`](../../packaging) gains the keywords, the character literal and its escapes, and the two operators. The program fuzzer gains generators for each form.

## Diagnostics

| Code | Rejection and primary span | Help | Status |
| --- | --- | --- | --- |
| E0260 | A character literal whose character is not one byte (non-ASCII, or a `\u{...}` escape above `FF`), at the literal | Use `Std.Core.Character` for a Unicode code point, or a string literal for text | implemented, proposed |
| E0261 | A malformed character literal: empty, several characters, unterminated, a raw control byte, or an unknown escape, at the literal or escape | List the escapes; for several characters, suggest double quotes | implemented, proposed |
| E0262 | A `..=` statement whose destination is not a `string`, or whose right side has a step, at the operator | Explain both forms of `..=` | implemented, proposed |
| E0263 | A range or chain of the wrong shape: a third bound, a step on a string chain, `..=` mixed with `..` or applied to strings in an expression, at the offending operator or colon | Show the range forms or the statement form | implemented, proposed |
| E0264 | A malformed `return if`: a missing `->`, a missing final `else`, or a block arm, at the expected position | Show the compact arm form | proposed |
| E0265 | A narrowing binding outside the whole condition or a top-level `&&` conjunct of an `if`, at the binding name | Name the allowed positions; for `while`, suggest an `if` inside the loop | implemented, proposed |
| E0266 | An `is not` narrowing inside an `&&` chain, at `not` | Use the whole condition, or test with `is` and bind in the true branch | implemented, proposed |
| E0267 | `typeof` without `(`, or with an operand that has no type (a `void` call, a module, an overload group, a function name), at the operand | Pass a value or a type | proposed |
| E0268 | A malformed Unicode escape in a string or character literal: no `{`, no digits, more than six, a non-hexadecimal character, or no `}`, at the escape | Show the form `\u{1F600}` | implemented, proposed |
| E0269 | A Unicode escape that is a surrogate or above `\u{10FFFF}`, at the escape | Name the scalar value ranges | implemented, proposed |
| E0270 | A string literal whose decoded bytes are not valid UTF-8 because of `\x` escapes, at the literal, labelled at the first offending escape | Suggest `\u{HH}` for that byte's code point | implemented, proposed |
| E1100 | A non-string operand of a string chain or of a `..=` statement, or a chain that mixes string and integer operands, at the first offending operand | Show its type and suggest `.to_string()` | implemented, proposed |

Every other rejection uses an existing code: operand types of `not` and `!==` as for `!` and `===`, subject and pattern rules of a narrowing as for `match.is` (E0411, E0490, E0605), integer literal ranges for character literals, readonly destinations, and return types.

## Edge cases

1. `not x`, `not (a && b)`, `not not x`, `!not x` are accepted and build the images of `!x`, `!(a && b)`, `!!x`, `!!x`.
2. `not` on a non-`bool` is the existing `!` operand error, naming `not`.
3. `x is not T && y` is `(x is not T) && y`; `x is not T` with a comment between `is` and `not`, or a line break, is the same form.
4. `x is not` at the end of input, or before `)`, reports a missing type at the next token; the parser recovers at `)` or `;`.
5. A binding named `not` or `typeof` is a parse error (reserved word); the search of every lane's sources found no such identifier (see [Migration](#migration)).
6. `if (x is y: T) { y.next(); }` binds `y`; `if (x is y: Iterator<T>)` and `if (x is y: Box<Iterator<T>>)` parse the nested `>>` as two closers.
7. `if (x is y: T) { } else { y; }` is the existing unknown-name error at `y`.
8. `if (x is y: T && y.ok())` sees `y` in the second conjunct; `if (y.ok() && x is y: T)` does not.
9. `if (x is y: T || z)` is E0265 at `y`; `if (!(x is y: T))`, `if ((x is y: T))`, `if (a + b is y: T)` (`is` binds more tightly, so the test of `b` is an operand of `+`) and `if (x is y: T && a || b)` (the chain is an operand of `||`) are E0265.
10. `if (c ? x is T : z)`, `f(a: x is T)`, `{a .. b : x is T}`-style steps and `match.is` arms keep their existing meaning.
11. `while (x is y: T)` is E0265 with the help to test inside the loop; `while (x is T)` is unchanged.
12. `if (x is not y: T) { return; } y.next();` binds `y` after the `if`; with `{ log(); }` as the true branch, `y` after the `if` is the unknown-name error.
13. `if (x is not y: T) { return; } else { y; }` binds `y` in the `else` and after the `if`.
14. `if (x is not y: T && c)` is E0266.
15. `if (x is x: Circle)` hides the subject `x` in the branch; `x = other;` inside it is the constant-assignment error.
16. `else if (x is y: T)` binds `y` in its own true branch only.
17. A narrowing of an `any` subject `if (v is n: int64)` unpacks the value; of a string subject `if (s is t: string)` is always true and binds `t`.
18. A narrowing to a type the subject never has is E0490, as in `match.is`.
19. `typeof(x)` evaluates `x` once; `typeof(f())` calls `f` once; `typeof(int32)` evaluates nothing.
20. `typeof(shape)` of a nil `Shape` is kind `Nil`; of a `Circle` held as `Shape` describes `Circle`; of an `any` holding `7 as int64` describes `int64`.
21. `typeof(value)` in a generic function specialized at `int32` and at `string` gives the two types.
22. `typeof(a) === typeof(b)` compares types; `typeof(a) == typeof(b)` is identity and is false for two evaluations.
23. `typeof` with no parentheses, `typeof()` and `typeof(print_line("x"))` are E0267; `typeof(x).name` is a member access on the result.
24. `typeof` works in a stripped image and in an image built from `.nir`, with the same names.
25. `'A'`, `'\n'`, `'\''`, `'\\'`, `'\"'`, `'"'`, `'\0'`, `'\x41'`, `'\xFF'` are accepted with the values 65, 10, 39, 92, 34, 34, 0, 65, 255.
26. `''`, `'ab'`, `'\q'`, `'\x4'`, `'\x4G'`, an unterminated `'a` and a tab between quotes are E0261.
27. `'é'` and `'\u{100}'` are E0260 with the help naming `Std.Core.Character`; `'\u{41}'` is 65 and `'\u{FF}'` 255.
28. `let c = 'A';` is a `byte`; `let i: int32 = 'A';` is 65; `let s: int8 = '\xFF';` is the existing literal range error.
29. `'A' .. 'Z'` without context is a `byte[]` range; `'a' ..= 'z'` contains 26 bytes.
30. `0 ..= 5` gives six elements; `5 ..= 0 : -1` six descending; `0 ..= 5 : 2` gives 0, 2, 4; `{0 ..= 5}` and `new(0 ..= 5)` for a list are accepted.
31. `250 ..= 255` as `uint8`, and an `int64` range ending at its maximum, do not overflow; `-128 ..= 127` as `int8` has 256 elements; `3 ..= 3` has one element.
32. A range over a whole 32-bit or 64-bit domain, such as `0 ..= 4294967295` as `uint32`, fails with the length failure that an over-long `..` range gives, not with an overflow trap.
33. `a ..= b ?? c` takes `b ?? c` as the stop; `a ..= b : 0` traps as a zero step does for `..`.
34. `1 .. 2 .. 3`, `0 ..= 2 .. 4` and `0 .. 2 ..= 4` are E0263 at the second operator.
35. `"a" .. "b" .. "c"` folds to `"abc"`; `x .. "a" .. "b"` folds to a chain of two operands.
36. `"turn " .. 3` and `3 .. "turn"` are E1100 at `3`.
37. `"a" .. "b" : 2` and `"a" ..= "b"` are E0263.
38. `(a .. b) .. c()` allocates twice and calls `c` after the inner allocation; `a .. b .. c()` allocates once after calling `c`.
39. A chain whose middle operand traps runs no later operand and allocates nothing.
40. A chain whose total length exceeds the `int32` limit traps after every operand ran, with the invalid-text length trap.
41. A chain under a heap budget that the final allocation exceeds traps with resource exhaustion after every operand ran.
42. `a .. b == c` is E1100 at `b == c`; `c ? a .. b : d` and `f(a .. b)` are accepted; `{a .. b}` with string bounds is E0263, since braces group ranges only.
43. `s ..= "x";` appends; `s ..= a .. b;` is one allocation; `s ..= (a .. b);` two; `items[next()] ..= "x";` calls `next` once.
44. `n ..= 5;` with an integer `n` is E0262 explaining both forms; `let r = n ..= 5;` is a range; `s ..= 5;` is E1100.
45. A read-only `string` destination of `..=` is the existing read-only assignment error.
46. `a !== b` builds the image of `!(a === b)`; `a !== b and c` is the syntax error of `a === b and c`; `!==` on operands without `equals` is the `===` error.
47. `a ! == b` and `a != = b`, written with spaces, are parse errors as today.
48. `return if (x > 0) -> "Positive"; else if (x == 0) -> "Zero"; else -> "Negative";` returns each of the three; `return if (c) -> a else -> b;` is accepted.
49. `return if (c) -> a;` with no `else`, `return if (c) a else -> b;` and a block arm are E0264.
50. `return x unless g;` evaluates `g` first; with a trapping `g`, `x` is not evaluated.
51. `return unless;`, `return unless(x);` and `return unless - 1;` keep their existing meaning with a binding or function named `unless`; `return unless unless flag;` is guarded by `flag`.
52. A non-`void` function that ends in `return x unless g;` is the existing missing-return error.
53. `return match (d) { ... } unless g;` and `return if (c) -> a else -> b unless g;` guard the whole value.
54. Comments and line breaks between any two tokens of these forms are accepted, and formatting twice gives the same text.
55. Incomplete editor input (`if (x is y:`, `if (x is not`, `typeof(`, `'`, `return if (c) ->`) reports one error at the end of input and keeps earlier declarations for the LSP.
56. `"\u{1F600}" == "😀"`, `"\u{e9}" == "é"`, `"\xC3\xA9" == "é"`, `"\0"` has length 1 and `"it\'s"` is `it's`.
57. `"\u{D800}"`, `"\u{110000}"` are E0269; `"\u1F600"`, `"\u{}"`, `"\u{1234567}"`, `"\u{12"` and `"\u{12G}"` are E0268, all at the escape.
58. `"\xE9"` and `"\xC3"` alone are E0270 with the suggestion `\u{E9}` (`\u{C3}`); `"\x4G"` is E0104.

## Order of work

| Step | Content | Image effect | Status |
| --- | --- | --- | --- |
| 1 | This note. | none | proposed |
| 2 | `not`, `is not`: lexer, parser, formatter, LSP, highlighting. | identical to `!` forms | implemented, proposed |
| 3 | Narrowing in `if`, then `is not` scoping and E0265/E0266. | identical to the `match.is` forms | implemented, proposed |
| 4 | Character literals, E0260/E0261, docs of literals and numbers. | identical to integer literals | implemented, proposed |
| 4b | Escapes: `\0`, `\'`, `\xHH` and `\u{H}` in string and character literals; E0268, E0269, E0270. | identical where unused | implemented, proposed |
| 5 | Inclusive ranges, string chains, `..=` statements, `!==`; NeuraIR 1.15 op 73, NVR 1.9 op 35, loader, assembler, tiers, counters; E0262, E0263, E1100. | new only where used | implemented, proposed |
| 6 | `return if` compact arms and `unless`; E0264. | identical to the `if` forms | proposed |
| 7 | `typeof`, NeuraIR op 74 and its elaboration, `Std.Reflect.Core.TypeInfo` and `Std.Reflect.Type.TypeKind`; E0267. | no NVR change | proposed |
| 8 | Language pages, status, changelog, examples, formatter round trip, LSP, editor grammar, fuzzer. | none | proposed |

## Gate and acceptance

"The same image" in this note means that every section of the two images is byte-identical except the source map (`SMAP`), whose spans follow the written columns: `!x` and `! x` already differ there with the compiler before this change. Comparisons by image therefore compare all other sections, and the same holds for images stripped of `NAME` and `SMAP`. Each step runs the targeted CTest groups (frontend, ui, diagnostics, backend, tier, ir-optimization, formatter, tooling, doc examples, stdlib) and compares the images of the identity corpus and every example built by the compiler before the step: a program that uses none of the step's forms builds byte-identical images. Each new form is compared with its hand-written equivalent by image bytes where this note says they are identical, and by observable output, traps and earlier effects otherwise, at tier 0 and with `--jit off`, `NEURA_TIER3=native`, `interp` and `off`, and `NEURA_TIER_CHECK=2`. The chain counter test proves one allocation and *B* copied bytes for chains of 2, 4 and 16 runtime operands, with and without optimization; a heap peak alone is not accepted as proof. The final gate is [the release gate](Release030.md).

## Migration

`not` and `typeof` become reserved words. A search on 2026-11-17 of every `.nra` file on the branches `mx-integration`, `m8-integration`, `a0-integration`, `r3-integration`, `codex/a1-integration`, `codex/a2-integration`, `codex/a4-integration`, `codex/a5-integration`, `codex/a6-integration`, `std-character`, `std-crypto` and `std-net`, and of the uncommitted files of their worktrees, found no identifier named `not` or `typeof` outside comments and strings, so no source changes. `'` was an invalid character (E0101) and `!==` and `..=` were token pairs that no valid program used, so every existing program keeps its meaning. `unless` is contextual and stays an ordinary name.

## Decisions taken without the owner

- **`not` binds like `!`.** The owner asked for `not` wherever `!` is allowed, so it is the same operator at the same level, not a looser Python-style `not`.
- **Narrowing positions.** A binding is allowed only as the whole `if` condition or a top-level `&&` conjunct, because only there does the true branch prove the test succeeded and only there can `is name :` not be confused with `?:`, a named argument or a range step.
- **`is not` scope.** An `is not` binding is visible in `else` and, when the true branch cannot complete, after the `if`, because that is the guard-clause idiom the stdlib modernization needs; inside `&&` it is rejected, because nothing proves the test failed on the false path.
- **No narrowing in loops.** `while`, `do` and `for` conditions are excluded in 0.3.0 to keep loop lowering and `continue` targets unchanged; an `if` inside the loop gives the same binding.
- **The binding is a snapshot constant.** It is made by `match.is`'s checked refinement so hooks and traps match, and reassigning the subject does not change it.
- **`typeof` is dynamic for references and `any`.** Callers ask for the type of a value most often to see its runtime class, and the static type of a reference is already written in the source; for value types the static and dynamic types coincide.
- **`typeof` of nil.** It returns kind `Nil` instead of trapping, so `typeof` is total and `typeof(x).kind` can test for absence.
- **`TypeInfo` is a record.** A final, immutable record gives value equality, hashing and text without hand-written members, can be shared between tasks, and can gain components later; `===` compares types.
- **Implementation of dynamic `typeof`.** A chain of class tests expanded after specialization needs no new runtime operation or image section and works in stripped images and through `.nir`; its cost grows with the number of candidate classes.
- **Character literals are `byte` integer literals.** They follow contextual integer typing so `b == 'A'` works for any integer `b`, default to `byte` because a character literal names a byte, accept `\xHH` for any byte, and reject anything outside one byte, pointing to `Std.Core.Character`.
- **`\u` in character literals up to FF (revised).** The first version of this note had no `\u` escape in character literals. After the owner approved Unicode escapes and asked that strings and character literals share their escapes, `'\u{E9}'` names the byte 0xE9, as `'A'` names 65, and anything above FF stays E0260 pointing to `Std.Core.Character`, so the one-byte rule is unchanged.
- **`\x` in strings must leave valid UTF-8.** Strings are UTF-8 text, so a `\x` escape is allowed where the bytes together stay valid UTF-8 and is E0270 otherwise; limiting `\x` in strings to 00-7F, as Rust does, was the rejected alternative.
- **`..=` has two roles.** By the owner's decision of 2026-11-17, `..=` in an expression is the inclusive range, and a statement that is exactly `place ..= text;` is concatenation assignment; an integer place is E0262 explaining both, so neither reading is chosen silently.
- **Chains are flat and n-ary.** Associativity does not change the text, and a flat chain is what lets the compiler allocate once; parentheses mark a separate allocation.
- **A new runtime operation for chains.** The existing `+` path builds a byte collection and then the text, so no existing operation can produce the text with one allocation; one state operation with a loader-validated signature is the smallest addition.
- **`!==` does not chain.** It follows `===`, which does not form comparison chains today; chains for both are a separate change.
- **The `;` before `else` in `return if` is optional.** The owner's request writes `return if (...) -> a else -> b;` and the module plan writes a `;` after each arm; both read unambiguously.
- **No guard on a bare `return`.** `return unless(x);` and similar statements are valid today with a binding named `unless`, so `unless` is a guard only after a complete value and stays an ordinary name everywhere else.
- **Block arms wait for the lifetime milestone.** They need its scope exits and its `yield`, which this branch does not have.

## Decisions taken in the implementation

- **Step 2: the `not` diagnostic.** The error for a non-`bool` operand of `!` (E0403) names no operator; for `not` it keeps the code, span and text and adds the note "'not' requires a bool operand, as '!' does", so the `!` diagnostics stay byte-identical. `x is not` with no type reports the existing "expected an identifier" of a missing type at the next token, as `x is` does.
- **Step 2: no editor grammar in this repository.** No VS Code grammar is tracked here (the search for one under `packaging` found none), so the keyword change reaches editors through the language server's semantic tokens, which mark `not` and `typeof` as keywords from their token kind. The debugger's condition expressions accept `not` beside `!`.
- **Step 3: lowering.** A narrowing `if` is lowered by its own function (`narrowing_if`), which builds the steps of `general_match` with `is_pattern` instead of synthesizing a `match.is` statement, because the branch bodies cannot be copied into one. The image tests in `tests/expressions/equivalence` compare it with the hand-written `match.is` forms. A scalar or string subject (`if (s is t: string)`) keeps the `_` arm without the coverage check that rejects it in `match.is` (E0491). A program that defines a lambda inside the branch differs from the `match.is` form only in the generated closure's name, which records the file it is written in.
- **Step 3: scopes and names.** The true branch is a block inside the bindings' scope, so a `let` in it may hide a binding, as a block inside any scope may. Two narrowings of one condition that bind the same name, and an after-`if` binding of `is not` whose name the enclosing block already declares, are the duplicate-local error E0301. Assigning a binding is the error that assigning a `match.is` binding gives, E0440 ("cannot assign twice"), since the binding is made the same way.
- **Step 3: rejections.** E0265 is reported in two ways: for a narrowing the parser recognized at the start of a top-level conjunct but that is not a conjunct root (`x is y: T || z`), and for a parse that fails at the `:` directly after `is name` or `is not name` in an `if`, `while`, `do` or `for` condition (parentheses, `!`, `a + b is y: T`, loops). Incomplete input such as `if (x is y:` reports the existing missing-type error at the end of the input. The language server lists the bindings as read-only names of the enclosing scope; hover with the refined type is left for step 8.
- **Step 4: character literals.** The token is an Integer whose text is the literal as written, so the formatter, the language server's number class and the hole lexer of interpolated strings need no change; the parser gives the expression the decimal value and marks it, and lowering gives a marked literal `byte` where no integer type is expected, in a generic call's inference as well. Two literals in one binary operation keep the existing rule that the left operand types the right one, so `'A' + 1` is a `byte` and `1 + 'A'` an `int32`; `-'A'` with no expected type is the unsigned-literal range error E0424, with a note giving the literal's value. Three quotes, `'''`, are E0261 with the suggestion `'\''`. A tab between quotes has the machine-applicable fix `'\t'`; an unterminated literal skips the rest of its line, as an unterminated string does. The debugger's expressions accept a character literal where they accept an integer.
- **Step 5: `!==` and `and`.** The note said `a !== b and c` keeps "the existing E0210 rule for `===`", but `a === b and c` is not E0210 today: `===` never starts a comparison chain, so the parser stops before `and` and reports the expected `;` or `)` (E0201 or E0203). `!==` is excluded from chains exactly as `===` is and gives the same errors, and E0210 stays the code of malformed `==`/`!=` chains. Lowering rewrites `a !== b` to `!(a === b)` before the operand checks of `!`, so the formatter and the syntax tree keep the written operator.
- **Step 5: one syntax node.** The parser builds one Range node for `a .. b`, `a ..= b` and `a .. b .. c`, recording each operator's span and spelling, the step's colon, and whether the node was written in parentheses or braces (parentheses used to leave no trace in the tree). Lowering checks the shape once it knows the type of the first operand, so `1 .. 2 .. 3`, `0 ..= 2 .. 4` and `0 .. 2 ..= 4` are E0263 there. The statement `place ..= right;` is recognized by the parser after the expression is read, whatever follows it (a `for` step and a setter clause included), so no `..=` statement silently becomes a discarded range; a step on it is E0262 at the operator, as the note asks, reported by the parser.
- **Step 5: counting `..=`.** The exclusive path builds the same operations as before. The inclusive path tests `low <= high`, takes `high - low` as the last offset, and saves the quotient in a temporary before limiting it, so the division (which also traps on a zero step) runs once. The over-long failure of both forms is the conversion of the count to `int32`, which traps as `source arithmetic failure (trap reason 1)`; the tests compare that text for `..` and for whole-domain `..=` ranges of `uint32` and `int64`.
- **Step 5: interim chain lowering.** The frontend half of step 5 lowers a chain, after folding, as the left fold of the string `+` that already exists, through one function (`concatenate_text`) that the runtime commit switches to `concat text`. Results, operand order and traps are those of the note, but each `+` allocates: a chain of *k* operands allocates *k* − 1 times and a trap in a later operand can follow an earlier allocation, until op 73 lands. Folding joins adjacent `String` constants after lowering, so a parenthesized chain of literals folds into its neighbours and one that encloses any other operand does not.
- **Step 5: E1100 for an integer first operand.** The role rule sends `3 .. "turn"` down the range path, where the stop fails to type as an integer. On that failing path only, the compiler lowers the stop again without an expected type, with its diagnostics discarded; a `string` result turns the error into E1100 at the integer first operand, as edge case 36 asks. A chain of three operands whose first is an integer and second a string is E1100 the same way, otherwise E0263.
- **Step 5: formatter spacing before `(`.** The formatter wrote no space between a binary operator and a following `(` (`0 ..(n)`, and `1 +(2)` for every binary operator). For the operators of this note it now writes one, `0 .. (n)`, `0 ..= (n)`, `a !== (b)`, as the Tooling paragraph asks; the other operators keep their existing output, since `-` and `!` are also prefix operators and changing them is a separate formatter change.
- **Step 5: the `..=` statement.** The parser turns a statement whose whole expression is an unparenthesized range starting with `..=` into an assignment with the operator `..=`, the place and the right side (a chain of the remaining operands when more follow); the operator's span is kept for E0262. Lowering uses the compound-assignment place functions (`capture_place`, `read_place`, `store_place`), reads the old value first, lowers each right operand as a chain operand, and joins them through the same `concatenate_text` as a chain, so the runtime commit changes both at once. A `..=` in the right side is E0263 at that operator.
- **Step 5: the runtime operation.** `concatenate_text` now emits `concat text` (op 73) for two or more operands after folding, and the converter emits NVR `concat-text` (op 35); a chain and a `..=` statement build no intermediate text. The machine sums the operand lengths, checks the length limit and the handle limit, reserves the one entry's cells with the operands as roots, and only then takes the operands' byte views and copies them into a block of the final length (`Heap::create_text_parts`); the block allocator never moves an entry, so the views stay valid. The test-only counters are `Heap::text_counters()`, surfaced as `RunResult::text_allocations` and `text_bytes_copied` (counted from after the constants are loaded) and read only by `backend-vm` (`string_chains_allocate_once`: 3 allocations and 32 bytes for chains of 2, 4 and 16 runtime operands, optimized and not, against 19 allocations for the same chains written with `+`; `string_chain_heap_budget_traps_after_operands` for edge case 41). Edge case 40 (a total above the `int32` limit) needs operands of more than 2 GiB and is checked by reading the code, not by a test. A chain of more than 255 operands runs on tier 1 (tier 2's operation width). A module-level constant whose value is a chain is a constant expression, as one written with `+` is. NeuraIR 1.14 belongs to the lifetime milestone; this reader rejects its tags 70 to 72, and the writer selects 1.15 only for a program with op 73.
