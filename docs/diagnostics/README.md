# Diagnostic codes

Each error that the compiler reports has a code: `E` and four digits, whose hundreds digit names the area of the compiler. Each warning has a code, `W` and four digits, and a name that `-W<name>`, `-Wno-<name>`, `#Allow`, and `#Deny` accept. `neura explain <CODE>` prints the page of a code, and `neura explain --list` lists the codes with their titles. [Tooling](../lang/Tooling.md#diagnostics) describes the formats in which diagnostics are printed.

A page states the rule, shows a program that breaks it with the error it gets, and shows the program fixed. The examples are compiled by the `doc-examples` test: a failing example must fail with its code, and a fixed one must compile. A few errors cannot be caused by a single source file; their pages say why, and show only the fix.

E0924 to E0929 are free for later tags, and W0006 to W0009 for later warnings.

## E01xx: Lexer

Errors about characters, literals, and comments.

| Code | Error |
| --- | --- |
| [E0101](E0101.md) | invalid character |
| [E0102](E0102.md) | invalid UTF-8 |
| [E0103](E0103.md) | unterminated string literal |
| [E0104](E0104.md) | invalid escape or character in a string literal |
| [E0105](E0105.md) | malformed numeric literal |
| [E0106](E0106.md) | misplaced digit separator |
| [E0107](E0107.md) | integer suffix on a floating-point literal |

## E02xx: Parser

Errors about the grammar.

| Code | Error |
| --- | --- |
| [E0201](E0201.md) | expected a semicolon |
| [E0202](E0202.md) | unexpected token |
| [E0203](E0203.md) | unclosed delimiter |
| [E0204](E0204.md) | expected an expression |
| [E0205](E0205.md) | declaration without a type or an initializer |
| [E0206](E0206.md) | invalid type syntax |
| [E0207](E0207.md) | misplaced variadic parameter |
| [E0208](E0208.md) | type annotation on a definition |
| [E0209](E0209.md) | enum without variants |
| [E0210](E0210.md) | malformed comparison chain |
| [E0211](E0211.md) | malformed scope statement |
| [E0212](E0212.md) | braces that do not enclose a range |
| [E0213](E0213.md) | malformed binding clause |
| [E0214](E0214.md) | parameter without a type |
| [E0215](E0215.md) | malformed class hook clause |
| [E0216](E0216.md) | class hook outside a declared class |
| [E0217](E0217.md) | malformed startup block |
| [E0218](E0218.md) | misplaced startup block |
| [E0219](E0219.md) | malformed defer statement |
| [E0220](E0220.md) | malformed using statement |
| [E0221](E0221.md) | malformed generator or yield |
| [E0222](E0222.md) | yield outside a generator |
| [E0225](E0225.md) | positional argument after a named argument |
| [E0226](E0226.md) | default value on a parameter that takes none |
| [E0227](E0227.md) | malformed interpolated string |
| [E0228](E0228.md) | malformed extension declaration or extension import |
| [E0240](E0240.md) | malformed html factory |
| [E0241](E0241.md) | unterminated template or stylesheet |
| [E0242](E0242.md) | mismatched or unclosed element |
| [E0243](E0243.md) | nesting a browser would repair |
| [E0244](E0244.md) | malformed tag |
| [E0245](E0245.md) | malformed or repeated attribute |
| [E0246](E0246.md) | unknown or malformed character reference |
| [E0247](E0247.md) | malformed hole |
| [E0248](E0248.md) | stray brace or control word in text |
| [E0249](E0249.md) | malformed control block |
| [E0250](E0250.md) | malformed binding directive |
| [E0251](E0251.md) | not allowed in a template |
| [E0252](E0252.md) | malformed css declaration |
| [E0253](E0253.md) | malformed stylesheet |
| [E0254](E0254.md) | malformed from clause |
| [E0255](E0255.md) | html or css form not allowed here |
| [E0256](E0256.md) | malformed CSS module construct |

## E03xx: Names

Errors about declarations, scopes, and name resolution.

| Code | Error |
| --- | --- |
| [E0230](E0230.md) | malformed match.is |
| [E0231](E0231.md) | pattern form that is not supported |
| [E0232](E0232.md) | postfix ? that needs parentheses |
| [E0233](E0233.md) | malformed optional access |
| [E0260](E0260.md) | character literal that is not one byte |
| [E0261](E0261.md) | malformed character literal |
| [E0262](E0262.md) | ..= statement whose destination is not a string |
| [E0263](E0263.md) | range or chain of the wrong shape |
| [E0264](E0264.md) | malformed return if |
| [E0265](E0265.md) | narrowing binding outside the condition of an if |
| [E0266](E0266.md) | is not narrowing inside an && chain |
| [E0267](E0267.md) | typeof without a value or a type |
| [E0268](E0268.md) | invalid Unicode escape |
| [E0269](E0269.md) | Unicode escape that is not a scalar value |
| [E0270](E0270.md) | string literal escapes that are not valid UTF-8 |
| [E0271](E0271.md) | dot tag without a name or group |
| [E0301](E0301.md) | duplicate local binding |
| [E0302](E0302.md) | duplicate declaration |
| [E0303](E0303.md) | duplicate parameter or field name |
| [E0304](E0304.md) | unknown name |
| [E0305](E0305.md) | unknown or inaccessible member |
| [E0306](E0306.md) | use before declaration |
| [E0307](E0307.md) | ambiguous promoted name |
| [E0308](E0308.md) | not a namespace |
| [E0309](E0309.md) | cyclic declaration |
| [E0310](E0310.md) | misuse of the discard name '_' |
| [E0311](E0311.md) | reserved name |
| [E0315](E0315.md) | ambiguous extension function |
| [E0316](E0316.md) | extension function used without a call |
| [E0318](E0318.md) | derivation mapping names no reachable member |
| [E0319](E0319.md) | invalid or repeated wire name |

## E04xx: Types and bindings

Errors about types, values, calls, and statements.

| Code | Error |
| --- | --- |
| [E0401](E0401.md) | assignment to a constant binding |
| [E0402](E0402.md) | invalid assignment target |
| [E0403](E0403.md) | mismatched types |
| [E0404](E0404.md) | local binding without an initializer |
| [E0405](E0405.md) | missing return |
| [E0406](E0406.md) | return does not match the function's result type |
| [E0407](E0407.md) | wrong number of arguments |
| [E0408](E0408.md) | no matching overload |
| [E0409](E0409.md) | ambiguous overload or cast |
| [E0410](E0410.md) | non-exhaustive match |
| [E0411](E0411.md) | invalid match subject |
| [E0412](E0412.md) | invalid match arm |
| [E0413](E0413.md) | operator not defined for the operand type |
| [E0414](E0414.md) | invalid indexing |
| [E0415](E0415.md) | invalid conversion |
| [E0416](E0416.md) | void in a value position |
| [E0417](E0417.md) | wrong number of type arguments |
| [E0418](E0418.md) | type argument does not satisfy its bound |
| [E0419](E0419.md) | explicit type arguments required |
| [E0420](E0420.md) | target-typed new without a class to construct |
| [E0421](E0421.md) | not a type of the required kind |
| [E0422](E0422.md) | expected a value |
| [E0423](E0423.md) | expression is not callable |
| [E0424](E0424.md) | literal out of range |
| [E0425](E0425.md) | type cannot be inferred |
| [E0426](E0426.md) | tuple arity mismatch |
| [E0427](E0427.md) | invalid enum variant use |
| [E0428](E0428.md) | invalid primitive reference |
| [E0429](E0429.md) | invalid range bounds |
| [E0430](E0430.md) | value is not iterable |
| [E0431](E0431.md) | statement outside its context |
| [E0432](E0432.md) | invalid for loop header |
| [E0433](E0433.md) | invalid overload group |
| [E0434](E0434.md) | function without a body |
| [E0435](E0435.md) | parenthesized type after new |
| [E0436](E0436.md) | destructuring count mismatch |
| [E0437](E0437.md) | destructuring annotation is not a tuple type |
| [E0440](E0440.md) | a deferred 'last' binding assigned twice |
| [E0441](E0441.md) | a deferred 'last' binding read before it is assigned |
| [E0442](E0442.md) | a deferred 'last' binding assigned in a loop |
| [E0443](E0443.md) | a closure captures a 'last' binding before it is assigned |
| [E0444](E0444.md) | a deferred 'last' binding without a type |
| [E0445](E0445.md) | a 'last' field assigned outside 'init' |
| [E0446](E0446.md) | a constructor that does not assign a 'last' field |
| [E0447](E0447.md) | a deferred 'last' binding that is not a local or an instance field |
| [E0448](E0448.md) | a value type that contains itself |
| [E0449](E0449.md) | a value type larger than its inline limit |
| [E0450](E0450.md) | a list literal without new |
| [E0451](E0451.md) | an array escapes its owner |
| [E0452](E0452.md) | a primitive borrow escapes its owner |
| [E0453](E0453.md) | an escaping closure requires explicit construction |
| [E0470](E0470.md) | return in a startup block |
| [E0471](E0471.md) | deferred call with a result |
| [E0472](E0472.md) | defer or using outside a function |
| [E0473](E0473.md) | using in a function that cannot return a close error |
| [E0474](E0474.md) | using resource that cannot be closed |
| [E0475](E0475.md) | scoped resource leaves its using block |
| [E0476](E0476.md) | yield with cleanup pending |
| [E0477](E0477.md) | storage that cannot outlive a suspension |
| [E0478](E0478.md) | return with a value in a generator |
| [E0479](E0479.md) | escaping capturing generator without new |
| [E0480](E0480.md) | conflicting inferred type arguments |
| [E0481](E0481.md) | lambda does not fit its callable context |
| [E0482](E0482.md) | unknown argument label |
| [E0483](E0483.md) | argument supplied twice |
| [E0484](E0484.md) | labels are not available for this call |
| [E0485](E0485.md) | default value is not a constant |
| [E0486](E0486.md) | invalid format specification in an interpolated string |
| [E0487](E0487.md) | invalid nameof operand |
| [E0488](E0488.md) | invalid extension function |

## E05xx: Classes and lifecycle

Errors about inheritance, interfaces, construction, destruction, field accessors, and anonymous classes.

| Code | Error |
| --- | --- |
| [E0490](E0490.md) | pattern that cannot match |
| [E0491](E0491.md) | unreachable match arm |
| [E0492](E0492.md) | value cannot be held in any |
| [E0493](E0493.md) | operation is not defined for any |
| [E0494](E0494.md) | optional access on a receiver that is never absent |
| [E0495](E0495.md) | invalid operands of ?? |
| [E0496](E0496.md) | ? on a value that is not a Result |
| [E0497](E0497.md) | ? outside a function that returns a Result |
| [E0498](E0498.md) | any at a boundary that would box it |
| [E0501](E0501.md) | invalid static member |
| [E0502](E0502.md) | invalid override |
| [E0503](E0503.md) | invalid inheritance |
| [E0504](E0504.md) | interface not satisfied |
| [E0505](E0505.md) | type cannot be constructed |
| [E0506](E0506.md) | invalid base constructor call |
| [E0507](E0507.md) | inaccessible constructor |
| [E0508](E0508.md) | member needs a receiver |
| [E0509](E0509.md) | invalid lifecycle function |
| [E0510](E0510.md) | field is read-only outside its class |
| [E0511](E0511.md) | invalid singleton |
| [E0512](E0512.md) | invalid cast declaration |
| [E0513](E0513.md) | reference field not definitely bound |
| [E0514](E0514.md) | invalid destroy |
| [E0515](E0515.md) | field is write-only outside its class |
| [E0516](E0516.md) | accessor on a field that is not exported |
| [E0517](E0517.md) | accessor on a static field |
| [E0518](E0518.md) | accessor on a member that is not a field |
| [E0519](E0519.md) | accessor outside a class |
| [E0520](E0520.md) | duplicate accessor |
| [E0521](E0521.md) | setter on a constant field |
| [E0522](E0522.md) | setter on a field named value |
| [E0523](E0523.md) | accessor on a primitive reference field |
| [E0524](E0524.md) | reference to a field with accessors |
| [E0530](E0530.md) | anonymous class field initializer that never runs |
| [E0531](E0531.md) | anonymous class uses an enclosing type parameter |
| [E0532](E0532.md) | unexported field in a fields-only anonymous class |
| [E0533](E0533.md) | new without the field initializers of an anonymous class |
| [E0534](E0534.md) | untyped field in a fields-only anonymous class |
| [E0536](E0536.md) | destructuring declared with last |
| [E0537](E0537.md) | constructor shorthand names no instance field |
| [E0538](E0538.md) | duplicate class hook clause |
| [E0539](E0539.md) | class hook replaces an inherited one without `.override` |
| [E0540](E0540.md) | `.override` on a class hook that overrides nothing |
| [E0541](E0541.md) | class hook on a class with synchronized methods |
| [E0542](E0542.md) | class hook that does not yield its class |
| [E0543](E0543.md) | class hook that returns a new object |
| [E0544](E0544.md) | class hook that may suspend |
| [E0545](E0545.md) | hooked class storage without an initializer |
| [E0546](E0546.md) | hooked class reference at a bodyless function |
| [E0550](E0550.md) | event emitted under a monitor guard |
| [E0560](E0560.md) | parameter names differ from the overridden method |
| [E0561](E0561.md) | defaults differ from the overridden method |
| [E0570](E0570.md) | invalid record declaration |
| [E0571](E0571.md) | record field of a type a record cannot hold |
| [E0572](E0572.md) | copy construction of a value that is not a record |
| [E0573](E0573.md) | invalid interface property |
| [E0574](E0574.md) | invalid virtual accessor |
| [E0575](E0575.md) | invalid accessor override |
| [E0576](E0576.md) | invalid static requirement or call of one |
| [E0577](E0577.md) | ambiguous static requirement |
| [E0578](E0578.md) | conformance declared by a module |
| [E0579](E0579.md) | unknown or misplaced derivation |
| [E0590](E0590.md) | unsupported JSON codec shape |
| [E0580](E0580.md) | type is not deeply immutable |
| [E0581](E0581.md) | frozen storage is exposed or changed |

## E06xx: Concurrency

Errors about tasks, scopes, and isolation.

| Code | Error |
| --- | --- |
| [E0601](E0601.md) | function is not task-safe |
| [E0602](E0602.md) | value is not transferable |
| [E0603](E0603.md) | invalid spawn |
| [E0604](E0604.md) | tasks combined with foreign code |
| [E0605](E0605.md) | scope-bound value leaves its scope |
| [E0606](E0606.md) | invalid select |
| [E0607](E0607.md) | scope block outside a function body |
| [E0608](E0608.md) | #ThreadSafe function that is not task-safe |
| [E0609](E0609.md) | #ThreadSafe class whose instances cannot be shared |
| [E0610](E0610.md) | #NotThreadSafe class that promises sharing |

## E07xx: Modules and imports

Errors about imports, artifacts, and C headers.

| Code | Error |
| --- | --- |
| [E0701](E0701.md) | unresolved import |
| [E0702](E0702.md) | import of an internal standard library module |
| [E0703](E0703.md) | ambiguous import |
| [E0704](E0704.md) | invalid import declaration |
| [E0705](E0705.md) | imported file does not declare the module |
| [E0706](E0706.md) | invalid NeuraIR artifact |
| [E0707](E0707.md) | conflicting NeuraIR artifact |
| [E0708](E0708.md) | C import failed |
| [E0709](E0709.md) | C header or library not found |
| [E0710](E0710.md) | invalid host or scheduler binding |
| [E0780](E0780.md) | kernel binding outside the standard library |
| [E0781](E0781.md) | kernel binding does not match its kernel |
| [E0782](E0782.md) | unknown kernel or kernel ABI version |
| [E0783](E0783.md) | malformed kernel binding |

## E08xx: Backend

Errors about circuit conversion and the image.

| Code | Error |
| --- | --- |
| [E0801](E0801.md) | not supported by circuit conversion |
| [E0802](E0802.md) | type without a circuit representation |
| [E0803](E0803.md) | missing entry point |
| [E0804](E0804.md) | more than one entry point |
| [E0805](E0805.md) | invalid entry point signature |
| [E0806](E0806.md) | external function without a runtime binding |
| [E0807](E0807.md) | literal not representable in the circuit |
| [E0808](E0808.md) | generic specialization limit exceeded |
| [E0899](E0899.md) | internal compiler error |

## E09xx: Tags

Errors about `#` tags before declarations ([Tags](../lang/Tags.md)).

| Code | Error |
| --- | --- |
| [E0900](E0900.md) | unknown tag |
| [E0901](E0901.md) | tag on a declaration it does not apply to |
| [E0902](E0902.md) | unknown or misplaced tag argument |
| [E0903](E0903.md) | missing tag argument |
| [E0904](E0904.md) | duplicate tag argument |
| [E0905](E0905.md) | tag argument of the wrong type |
| [E0906](E0906.md) | duplicate tag |
| [E0907](E0907.md) | conflicting tags |
| [E0908](E0908.md) | reserved tag |
| [E0909](E0909.md) | tags without a declaration |
| [E0910](E0910.md) | malformed tag |
| [E0911](E0911.md) | use of an internal declaration outside its library |
| [E0912](E0912.md) | extension of a final class |
| [E0913](E0913.md) | override of a final method |
| [E0914](E0914.md) | extension of a sealed type outside its module |
| [E0915](E0915.md) | unknown warning in a tag |
| [E0916](E0916.md) | result tag on a function without a result |
| [E0917](E0917.md) | #Pure function that is not pure |
| [E0918](E0918.md) | assignable field in an #Immutable class |
| [E0919](E0919.md) | field of a mutable type in an #Immutable class |
| [E0920](E0920.md) | #Immutable class with a base or subclass that is not #Immutable |
| [E0921](E0921.md) | #Synchronized on a method without a body to guard |
| [E0922](E0922.md) | override of a synchronized method that is not synchronized |
| [E0923](E0923.md) | #HostEffects on a function that is not a host binding without a body |
| [E1100](E1100.md) | string chain operand that is not a string |

## E10xx: Applications

Errors about application forms and their build limits.

| Code | Error |
| --- | --- |
| [E1001](E1001.md) | unknown element |
| [E1002](E1002.md) | unknown attribute, property or event |
| [E1003](E1003.md) | modifier not valid for this event |
| [E1004](E1004.md) | value cannot be rendered |
| [E1005](E1005.md) | attribute or property value of the wrong type |
| [E1006](E1006.md) | handler does not match the event |
| [E1007](E1007.md) | URL not allowed |
| [E1008](E1008.md) | value cannot be iterated by a template |
| [E1009](E1009.md) | key of the wrong type |
| [E1010](E1010.md) | effect not allowed while rendering |
| [E1011](E1011.md) | template or stylesheet file cannot be used |
| [E1012](E1012.md) | not a class of the CSS module |
| [E1013](E1013.md) | stylesheet import or asset cannot be resolved |
| [E1014](E1014.md) | invalid static document structure |
| [E1015](E1015.md) | template or stylesheet exceeds a limit |

## E10xx: Application projects

Errors about projects and their artifacts.

| Code | Error |
| --- | --- |
| [E1020](E1020.md) | malformed project manifest |
| [E1021](E1021.md) | unsupported manifest schema or language version |
| [E1022](E1022.md) | lock file missing or out of date |
| [E1023](E1023.md) | invalid target |
| [E1024](E1024.md) | entry module without the required entry |
| [E1025](E1025.md) | path outside the project |
| [E1026](E1026.md) | content does not match its digest |
| [E1027](E1027.md) | capability used but not declared |
| [E1028](E1028.md) | limit out of range |
| [E1029](E1029.md) | bundle cannot be written |
| [E1030](E1030.md) | module not available to this package or target |
| [E1031](E1031.md) | unknown or incompatible provider |
| [E1032](E1032.md) | dependency not pinned or not found |
| [E1033](E1033.md) | package cannot be built here |

## E13xx: Reflection

Codes reported for `Std.Reflect` ([design](../design/Reflect030.md)).

| Code | Error |
| --- | --- |
| [E1300](E1300.md) | a type that Std.Reflect.Proxy cannot proxy |
| [E1301](E1301.md) | a value a Std.Reflect dynamic value cannot hold |
| [E1302](E1302.md) | a class with two reflection policies |

## E14xx: Laws and proofs

Errors reported by the law checker. Some codes are reserved for later phases of the [design](../design/Laws031.md).

| Code | Error |
| --- | --- |
| [E1401](E1401.md) | reflexivity proof does not match its goal |
| [E1402](E1402.md) | rewrite motive does not match its goal |
| [E1403](E1403.md) | proof leaves a goal undischarged |
| [E1404](E1404.md) | concrete module leaves an inherited law unproven |
| [E1405](E1405.md) | law statement cannot be overridden |
| [E1406](E1406.md) | override invalidates an inherited proof |
| [E1407](E1407.md) | proof match does not cover every case |
| [E1408](E1408.md) | invalid witness for a constructor or contradiction proof |
| [E1409](E1409.md) | law or proof term has the wrong type |
| [E1410](E1410.md) | law uses an impure or forbidden operation |
| [E1411](E1411.md) | total equality uses an operation that can trap |
| [E1412](E1412.md) | proof does not establish its trap claim |
| [E1413](E1413.md) | recursive proof does not decrease |
| [E1414](E1414.md) | law dependencies form a cycle |
| [E1415](E1415.md) | proof normalization fuel exhausted |
| [E1416](E1416.md) | closed unit contains an abstract law |
| [E1417](E1417.md) | class invariant field is writable without its guard |
| [E1418](E1418.md) | mutable reference escapes an invariant field |
| [E1419](E1419.md) | independent proof checker disagrees with the compiler |
| [E1420](E1420.md) | axiom declared outside the trusted standard library |
| [E1421](E1421.md) | law feature unavailable |

## Warnings

Warnings do not stop compilation unless `-Werror` or a `#Deny` tag makes them errors. Warnings in the standard library are hidden unless `--warn-library` is given.

| Code | Name | Warning |
| --- | --- | --- |
| [W0001](W0001.md) | `deprecated` | use of a deprecated declaration |
| [W0002](W0002.md) | `experimental` | use of an experimental declaration |
| [W0003](W0003.md) | `unused-result` | discarded result of a #NoDiscard call |
| [W0004](W0004.md) | `inline` | #Inline(always: true) function that cannot be inlined |
| [W0005](W0005.md) | `setter-never-stores` | setter that never stores its field |
| [W0006](W0006.md) | `nil-access` | access through a reference that may be nil |
| [W0007](W0007.md) | `nil-argument` | possibly nil value goes into a non-nil binding |
