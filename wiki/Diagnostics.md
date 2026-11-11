<!-- Generated from docs/diagnostics/README.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Diagnostic codes

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

Each error that the compiler reports has a code: `E` and four digits, whose hundreds digit names the area of the compiler. Each warning has a code, `W` and four digits, and a name that `-W<name>`, `-Wno-<name>`, `#Allow`, and `#Deny` accept. `neura explain <CODE>` prints the page of a code, and `neura explain --list` lists the codes with their titles. [Tooling](Tooling#diagnostics) describes the formats in which diagnostics are printed.

A page states the rule, shows a program that breaks it with the error it gets, and shows the program fixed. The examples are compiled by the `doc-examples` test: a failing example must fail with its code, and a fixed one must compile. A few errors cannot be caused by a single source file; their pages say why, and show only the fix.

E0923 to E0929 are free for later tags, and W0006 to W0009 for later warnings.

## E01xx: Lexer

Errors about characters, literals, and comments.

| Code | Error |
| --- | --- |
| [E0101](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0101.md) | invalid character |
| [E0102](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0102.md) | invalid UTF-8 |
| [E0103](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0103.md) | unterminated string literal |
| [E0104](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0104.md) | invalid escape or character in a string literal |
| [E0105](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0105.md) | malformed numeric literal |

## E02xx: Parser

Errors about the grammar.

| Code | Error |
| --- | --- |
| [E0201](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0201.md) | expected a semicolon |
| [E0202](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0202.md) | unexpected token |
| [E0203](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0203.md) | unclosed delimiter |
| [E0204](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0204.md) | expected an expression |
| [E0205](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0205.md) | declaration without a type or an initializer |
| [E0206](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0206.md) | invalid type syntax |
| [E0207](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0207.md) | misplaced variadic parameter |
| [E0208](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0208.md) | type annotation on a definition |
| [E0209](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0209.md) | enum without variants |
| [E0210](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0210.md) | malformed comparison chain |
| [E0211](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0211.md) | malformed scope statement |
| [E0212](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0212.md) | braces that do not enclose a range |
| [E0213](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0213.md) | malformed binding clause |

## E03xx: Names

Errors about declarations, scopes, and name resolution.

| Code | Error |
| --- | --- |
| [E0301](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0301.md) | duplicate local binding |
| [E0302](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0302.md) | duplicate declaration |
| [E0303](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0303.md) | duplicate parameter or field name |
| [E0304](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0304.md) | unknown name |
| [E0305](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0305.md) | unknown or inaccessible member |
| [E0306](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0306.md) | use before declaration |
| [E0307](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0307.md) | ambiguous promoted name |
| [E0308](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0308.md) | not a namespace |
| [E0309](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0309.md) | cyclic declaration |
| [E0310](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0310.md) | misuse of the discard name '_' |
| [E0311](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0311.md) | reserved name |

## E04xx: Types and bindings

Errors about types, values, calls, and statements.

| Code | Error |
| --- | --- |
| [E0401](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0401.md) | assignment to a constant binding |
| [E0402](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0402.md) | invalid assignment target |
| [E0403](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0403.md) | mismatched types |
| [E0404](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0404.md) | local binding without an initializer |
| [E0405](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0405.md) | missing return |
| [E0406](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0406.md) | return does not match the function's result type |
| [E0407](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0407.md) | wrong number of arguments |
| [E0408](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0408.md) | no matching overload |
| [E0409](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0409.md) | ambiguous overload or cast |
| [E0410](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0410.md) | non-exhaustive match |
| [E0411](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0411.md) | invalid match subject |
| [E0412](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0412.md) | invalid match arm |
| [E0413](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0413.md) | operator not defined for the operand type |
| [E0414](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0414.md) | invalid indexing |
| [E0415](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0415.md) | invalid conversion |
| [E0416](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0416.md) | void in a value position |
| [E0417](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0417.md) | wrong number of type arguments |
| [E0418](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0418.md) | type argument does not satisfy its bound |
| [E0419](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0419.md) | explicit type arguments required |
| [E0420](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0420.md) | target-typed new without a class to construct |
| [E0421](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0421.md) | not a type of the required kind |
| [E0422](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0422.md) | expected a value |
| [E0423](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0423.md) | expression is not callable |
| [E0424](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0424.md) | literal out of range |
| [E0425](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0425.md) | type cannot be inferred |
| [E0426](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0426.md) | tuple arity mismatch |
| [E0427](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0427.md) | invalid enum variant use |
| [E0428](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0428.md) | invalid primitive reference |
| [E0429](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0429.md) | invalid range bounds |
| [E0430](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0430.md) | value is not iterable |
| [E0431](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0431.md) | statement outside its context |
| [E0432](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0432.md) | invalid for loop header |
| [E0433](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0433.md) | invalid overload group |
| [E0434](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0434.md) | function without a body |
| [E0435](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0435.md) | parenthesized type after new |
| [E0436](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0436.md) | destructuring count mismatch |
| [E0437](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0437.md) | destructuring annotation is not a tuple type |
| [E0440](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0440.md) | a deferred 'last' binding assigned twice |
| [E0441](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0441.md) | a deferred 'last' binding read before it is assigned |
| [E0442](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0442.md) | a deferred 'last' binding assigned in a loop |
| [E0443](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0443.md) | a closure captures a 'last' binding before it is assigned |
| [E0444](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0444.md) | a deferred 'last' binding without a type |
| [E0445](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0445.md) | a 'last' field assigned outside 'init' |
| [E0446](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0446.md) | a constructor that does not assign a 'last' field |
| [E0447](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0447.md) | a deferred 'last' binding that is not a local or an instance field |

## E05xx: Classes and lifecycle

Errors about inheritance, interfaces, construction, destruction, field accessors, and anonymous classes.

| Code | Error |
| --- | --- |
| [E0501](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0501.md) | invalid static member |
| [E0502](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0502.md) | invalid override |
| [E0503](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0503.md) | invalid inheritance |
| [E0504](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0504.md) | interface not satisfied |
| [E0505](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0505.md) | type cannot be constructed |
| [E0506](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0506.md) | invalid base constructor call |
| [E0507](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0507.md) | inaccessible constructor |
| [E0508](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0508.md) | member needs a receiver |
| [E0509](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0509.md) | invalid lifecycle function |
| [E0510](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0510.md) | field is read-only outside its class |
| [E0511](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0511.md) | invalid singleton |
| [E0512](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0512.md) | invalid cast declaration |
| [E0513](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0513.md) | reference field not definitely bound |
| [E0514](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0514.md) | invalid destroy |
| [E0515](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0515.md) | field is write-only outside its class |
| [E0516](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0516.md) | accessor on a field that is not exported |
| [E0517](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0517.md) | accessor on a static field |
| [E0518](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0518.md) | accessor on a member that is not a field |
| [E0519](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0519.md) | accessor outside a class |
| [E0520](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0520.md) | duplicate accessor |
| [E0521](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0521.md) | setter on a constant field |
| [E0522](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0522.md) | setter on a field named value |
| [E0523](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0523.md) | accessor on a primitive reference field |
| [E0524](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0524.md) | reference to a field with accessors |
| [E0530](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0530.md) | anonymous class field initializer that never runs |
| [E0531](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0531.md) | anonymous class uses an enclosing type parameter |
| [E0532](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0532.md) | unexported field in a fields-only anonymous class |
| [E0533](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0533.md) | new without the field initializers of an anonymous class |
| [E0534](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0534.md) | untyped field in a fields-only anonymous class |
| [E0536](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0536.md) | destructuring declared with last |

## E06xx: Concurrency

Errors about tasks, scopes, and isolation.

| Code | Error |
| --- | --- |
| [E0601](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0601.md) | function is not task-safe |
| [E0602](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0602.md) | value is not transferable |
| [E0603](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0603.md) | invalid spawn |
| [E0604](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0604.md) | tasks combined with foreign code |
| [E0605](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0605.md) | scope-bound value leaves its scope |
| [E0606](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0606.md) | invalid select |
| [E0607](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0607.md) | scope block outside a function body |
| [E0608](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0608.md) | #ThreadSafe function that is not task-safe |
| [E0609](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0609.md) | #ThreadSafe class whose instances cannot be shared |
| [E0610](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0610.md) | #NotThreadSafe class that promises sharing |

## E07xx: Modules and imports

Errors about imports, artifacts, and C headers.

| Code | Error |
| --- | --- |
| [E0701](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0701.md) | unresolved import |
| [E0702](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0702.md) | import of an internal standard library module |
| [E0703](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0703.md) | ambiguous import |
| [E0704](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0704.md) | invalid import declaration |
| [E0705](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0705.md) | imported file does not declare the module |
| [E0706](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0706.md) | invalid NeuraIR artifact |
| [E0707](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0707.md) | conflicting NeuraIR artifact |
| [E0708](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0708.md) | C import failed |
| [E0709](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0709.md) | C header or library not found |
| [E0710](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0710.md) | invalid host or scheduler binding |
| [E0780](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0780.md) | kernel binding outside the standard library |
| [E0781](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0781.md) | kernel binding does not match its kernel |
| [E0782](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0782.md) | unknown kernel or kernel ABI version |
| [E0783](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0783.md) | malformed kernel binding |

## E08xx: Backend

Errors about circuit conversion and the image.

| Code | Error |
| --- | --- |
| [E0801](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0801.md) | not supported by circuit conversion |
| [E0802](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0802.md) | type without a circuit representation |
| [E0803](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0803.md) | missing entry point |
| [E0804](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0804.md) | more than one entry point |
| [E0805](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0805.md) | invalid entry point signature |
| [E0806](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0806.md) | external function without a runtime binding |
| [E0807](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0807.md) | literal not representable in the circuit |
| [E0808](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0808.md) | generic specialization limit exceeded |
| [E0899](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0899.md) | internal compiler error |

## E09xx: Tags

Errors about `#` tags before declarations ([Tags](Tags)).

| Code | Error |
| --- | --- |
| [E0900](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0900.md) | unknown tag |
| [E0901](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0901.md) | tag on a declaration it does not apply to |
| [E0902](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0902.md) | unknown or misplaced tag argument |
| [E0903](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0903.md) | missing tag argument |
| [E0904](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0904.md) | duplicate tag argument |
| [E0905](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0905.md) | tag argument of the wrong type |
| [E0906](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0906.md) | duplicate tag |
| [E0907](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0907.md) | conflicting tags |
| [E0908](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0908.md) | reserved tag |
| [E0909](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0909.md) | tags without a declaration |
| [E0910](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0910.md) | malformed tag |
| [E0911](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0911.md) | use of an internal declaration outside its library |
| [E0912](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0912.md) | extension of a final class |
| [E0913](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0913.md) | override of a final method |
| [E0914](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0914.md) | extension of a sealed type outside its module |
| [E0915](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0915.md) | unknown warning in a tag |
| [E0916](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0916.md) | result tag on a function without a result |
| [E0917](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0917.md) | #Pure function that is not pure |
| [E0918](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0918.md) | assignable field in an #Immutable class |
| [E0919](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0919.md) | field of a mutable type in an #Immutable class |
| [E0920](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0920.md) | #Immutable class with a base or subclass that is not #Immutable |
| [E0921](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0921.md) | #Synchronized on a method without a body to guard |
| [E0922](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0922.md) | override of a synchronized method that is not synchronized |

## Warnings

Warnings do not stop compilation unless `-Werror` or a `#Deny` tag makes them errors. Warnings in the standard library are hidden unless `--warn-library` is given.

| Code | Name | Warning |
| --- | --- | --- |
| [W0001](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0001.md) | `deprecated` | use of a deprecated declaration |
| [W0002](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0002.md) | `experimental` | use of an experimental declaration |
| [W0003](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0003.md) | `unused-result` | discarded result of a #NoDiscard call |
| [W0004](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0004.md) | `inline` | #Inline(always: true) function that cannot be inlined |
| [W0005](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0005.md) | `setter-never-stores` | setter that never stores its field |
