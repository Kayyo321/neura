<!-- Generated from docs/diagnostics/README.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Diagnostic codes

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

Each error that the compiler reports has a code: `E` and four digits, whose hundreds digit names the area of the compiler. Each warning has a code, `W` and four digits, and a name that `-W<name>`, `-Wno-<name>`, `#Allow`, and `#Deny` accept. `neura explain <CODE>` prints the page of a code, and `neura explain --list` lists the codes with their titles. [Tooling](Tooling#diagnostics) describes the formats in which diagnostics are printed.

A page states the rule, shows a program that breaks it with the error it gets, and shows the program fixed. The examples are compiled by the `doc-examples` test: a failing example must fail with its code, and a fixed one must compile. A few errors cannot be caused by a single source file; their pages say why, and show only the fix.

E0924 to E0929 are free for later tags, and W0006 to W0009 for later warnings.

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
| [E0214](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0214.md) | parameter without a type |
| [E0215](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0215.md) | malformed class hook clause |
| [E0216](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0216.md) | class hook outside a declared class |
| [E0217](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0217.md) | malformed startup block |
| [E0218](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0218.md) | misplaced startup block |
| [E0219](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0219.md) | malformed defer statement |
| [E0220](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0220.md) | malformed using statement |
| [E0221](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0221.md) | malformed generator or yield |
| [E0222](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0222.md) | yield outside a generator |
| [E0225](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0225.md) | positional argument after a named argument |
| [E0226](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0226.md) | default value on a parameter that takes none |
| [E0227](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0227.md) | malformed interpolated string |
| [E0228](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0228.md) | malformed extension declaration or extension import |
| [E0240](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0240.md) | malformed html factory |
| [E0241](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0241.md) | unterminated template or stylesheet |
| [E0242](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0242.md) | mismatched or unclosed element |
| [E0243](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0243.md) | nesting a browser would repair |
| [E0244](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0244.md) | malformed tag |
| [E0245](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0245.md) | malformed or repeated attribute |
| [E0246](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0246.md) | unknown or malformed character reference |
| [E0247](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0247.md) | malformed hole |
| [E0248](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0248.md) | stray brace or control word in text |
| [E0249](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0249.md) | malformed control block |
| [E0250](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0250.md) | malformed binding directive |
| [E0251](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0251.md) | not allowed in a template |
| [E0252](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0252.md) | malformed css declaration |
| [E0253](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0253.md) | malformed stylesheet |
| [E0254](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0254.md) | malformed from clause |
| [E0255](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0255.md) | html or css form not allowed here |
| [E0256](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0256.md) | malformed CSS module construct |

## E03xx: Names

Errors about declarations, scopes, and name resolution.

| Code | Error |
| --- | --- |
| [E0230](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0230.md) | malformed match.is |
| [E0231](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0231.md) | pattern form that is not supported |
| [E0232](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0232.md) | postfix ? that needs parentheses |
| [E0233](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0233.md) | malformed optional access |
| [E0260](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0260.md) | character literal that is not one byte |
| [E0261](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0261.md) | malformed character literal |
| [E0262](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0262.md) | ..= statement whose destination is not a string |
| [E0263](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0263.md) | range or chain of the wrong shape |
| [E0264](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0264.md) | malformed return if |
| [E0265](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0265.md) | narrowing binding outside the condition of an if |
| [E0266](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0266.md) | is not narrowing inside an && chain |
| [E0267](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0267.md) | typeof without a value or a type |
| [E0268](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0268.md) | invalid Unicode escape |
| [E0269](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0269.md) | Unicode escape that is not a scalar value |
| [E0270](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0270.md) | string literal escapes that are not valid UTF-8 |
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
| [E0315](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0315.md) | ambiguous extension function |
| [E0316](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0316.md) | extension function used without a call |
| [E0318](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0318.md) | derivation mapping names no reachable member |
| [E0319](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0319.md) | invalid or repeated wire name |

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
| [E0448](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0448.md) | a value type that contains itself |
| [E0449](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0449.md) | a value type larger than its inline limit |
| [E0450](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0450.md) | a list literal without new |
| [E0451](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0451.md) | an array escapes its owner |
| [E0452](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0452.md) | a primitive borrow escapes its owner |
| [E0453](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0453.md) | an escaping closure requires explicit construction |
| [E0470](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0470.md) | return in a startup block |
| [E0471](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0471.md) | deferred call with a result |
| [E0472](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0472.md) | defer or using outside a function |
| [E0473](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0473.md) | using in a function that cannot return a close error |
| [E0474](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0474.md) | using resource that cannot be closed |
| [E0475](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0475.md) | scoped resource leaves its using block |
| [E0476](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0476.md) | yield with cleanup pending |
| [E0477](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0477.md) | storage that cannot outlive a suspension |
| [E0478](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0478.md) | return with a value in a generator |
| [E0479](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0479.md) | escaping capturing generator without new |
| [E0480](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0480.md) | conflicting inferred type arguments |
| [E0481](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0481.md) | lambda does not fit its callable context |
| [E0482](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0482.md) | unknown argument label |
| [E0483](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0483.md) | argument supplied twice |
| [E0484](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0484.md) | labels are not available for this call |
| [E0485](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0485.md) | default value is not a constant |
| [E0486](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0486.md) | invalid format specification in an interpolated string |
| [E0487](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0487.md) | invalid nameof operand |
| [E0488](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0488.md) | invalid extension function |

## E05xx: Classes and lifecycle

Errors about inheritance, interfaces, construction, destruction, field accessors, and anonymous classes.

| Code | Error |
| --- | --- |
| [E0490](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0490.md) | pattern that cannot match |
| [E0491](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0491.md) | unreachable match arm |
| [E0492](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0492.md) | value cannot be held in any |
| [E0493](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0493.md) | operation is not defined for any |
| [E0494](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0494.md) | optional access on a receiver that is never absent |
| [E0495](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0495.md) | invalid operands of ?? |
| [E0496](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0496.md) | ? on a value that is not a Result |
| [E0497](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0497.md) | ? outside a function that returns a Result |
| [E0498](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0498.md) | any at a boundary that would box it |
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
| [E0537](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0537.md) | constructor shorthand names no instance field |
| [E0538](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0538.md) | duplicate class hook clause |
| [E0539](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0539.md) | class hook replaces an inherited one without `.override` |
| [E0540](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0540.md) | `.override` on a class hook that overrides nothing |
| [E0541](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0541.md) | class hook on a class with synchronized methods |
| [E0542](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0542.md) | class hook that does not yield its class |
| [E0543](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0543.md) | class hook that returns a new object |
| [E0544](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0544.md) | class hook that may suspend |
| [E0545](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0545.md) | hooked class storage without an initializer |
| [E0546](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0546.md) | hooked class reference at a bodyless function |
| [E0550](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0550.md) | event emitted under a monitor guard |
| [E0560](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0560.md) | parameter names differ from the overridden method |
| [E0561](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0561.md) | defaults differ from the overridden method |
| [E0570](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0570.md) | invalid record declaration |
| [E0571](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0571.md) | record field of a type a record cannot hold |
| [E0572](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0572.md) | copy construction of a value that is not a record |
| [E0573](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0573.md) | invalid interface property |
| [E0574](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0574.md) | invalid virtual accessor |
| [E0575](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0575.md) | invalid accessor override |
| [E0576](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0576.md) | invalid static requirement or call of one |
| [E0577](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0577.md) | ambiguous static requirement |
| [E0578](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0578.md) | conformance declared by a module |
| [E0579](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0579.md) | unknown or misplaced derivation |
| [E0590](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0590.md) | unsupported JSON codec shape |
| [E0580](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0580.md) | type is not deeply immutable |
| [E0581](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0581.md) | frozen storage is exposed or changed |

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
| [E0923](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0923.md) | #HostEffects on a function that is not a host binding without a body |
| [E1100](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1100.md) | string chain operand that is not a string |

## E10xx: Applications

Errors about application forms and their build limits.

| Code | Error |
| --- | --- |
| [E1001](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1001.md) | unknown element |
| [E1002](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1002.md) | unknown attribute, property or event |
| [E1003](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1003.md) | modifier not valid for this event |
| [E1004](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1004.md) | value cannot be rendered |
| [E1005](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1005.md) | attribute or property value of the wrong type |
| [E1006](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1006.md) | handler does not match the event |
| [E1007](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1007.md) | URL not allowed |
| [E1008](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1008.md) | value cannot be iterated by a template |
| [E1009](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1009.md) | key of the wrong type |
| [E1010](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1010.md) | effect not allowed while rendering |
| [E1011](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1011.md) | template or stylesheet file cannot be used |
| [E1012](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1012.md) | not a class of the CSS module |
| [E1013](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1013.md) | stylesheet import or asset cannot be resolved |
| [E1014](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1014.md) | invalid static document structure |
| [E1015](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1015.md) | template or stylesheet exceeds a limit |

## E10xx: Application projects

Errors about projects and their artifacts.

| Code | Error |
| --- | --- |
| [E1020](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1020.md) | malformed project manifest |
| [E1021](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1021.md) | unsupported manifest schema or language version |
| [E1022](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1022.md) | lock file missing or out of date |
| [E1023](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1023.md) | invalid target |
| [E1024](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1024.md) | entry module without the required entry |
| [E1025](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1025.md) | path outside the project |
| [E1026](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1026.md) | content does not match its digest |
| [E1027](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1027.md) | capability used but not declared |
| [E1028](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1028.md) | limit out of range |
| [E1029](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1029.md) | bundle cannot be written |
| [E1030](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1030.md) | module not available to this package or target |
| [E1031](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1031.md) | unknown or incompatible provider |
| [E1032](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1032.md) | dependency not pinned or not found |
| [E1033](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1033.md) | package cannot be built here |

## E13xx: Reflection

Codes reported for `Std.Reflect` ([design](https://github.com/Kayyo321/neura/blob/main/docs/design/Reflect030.md)).

| Code | Error |
| --- | --- |
| [E1300](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1300.md) | a type that Std.Reflect.Proxy cannot proxy |
| [E1301](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1301.md) | a value a Std.Reflect dynamic value cannot hold |
| [E1302](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E1302.md) | a class with two reflection policies |

## Warnings

Warnings do not stop compilation unless `-Werror` or a `#Deny` tag makes them errors. Warnings in the standard library are hidden unless `--warn-library` is given.

| Code | Name | Warning |
| --- | --- | --- |
| [W0001](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0001.md) | `deprecated` | use of a deprecated declaration |
| [W0002](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0002.md) | `experimental` | use of an experimental declaration |
| [W0003](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0003.md) | `unused-result` | discarded result of a #NoDiscard call |
| [W0004](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0004.md) | `inline` | #Inline(always: true) function that cannot be inlined |
| [W0005](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0005.md) | `setter-never-stores` | setter that never stores its field |
