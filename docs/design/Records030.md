# Neura 0.3.0: records, property contracts, static requirements and derivation

This note fixes the grammar and rules of the 0.3.0 data-and-contracts milestone before it is implemented, as [the 0.3.0 baseline](Release030.md) requires: immutable record classes with copy construction, interface properties and virtual accessors, static interface requirements with a small set of generic-math contracts, and compiler-owned derivation of JSON codecs. It builds on [grouped parameters and `=> set`](Parameters030.md), [semantic facts](Representation030.md) and [storage and construction](Storage030.md). The rows move to [Types](../lang/Types.md), [Polymorphism](../lang/Polymorphism.md), [Object lifecycle](../lang/ObjectLifecycle.md), [the standard library](../lang/Stdlib.md) and [the NeuraIR format](../NirFormat.md) in the change that implements them.

Status: *proposed* on 2026-11-17. The owner was away and had asked for autonomous work toward 0.3.0, so the rows are implemented as written by autonomous agents and await the owner's confirmation; "proposed" stays until then. A row the owner changes is changed here and on the page that owns it in one later change.

The milestone adds no NVR operation and changes no image of a program that uses none of its features. It adds one NeuraIR operation (the call of a static requirement through a type parameter), five symbol attributes, and value members for payload enums. The calls milestone's note was written beside this one: it fixes a named argument as `name: expression` and makes the parameter names of an exported declaration part of its signature. The notes of the hooks and matching milestones were not written yet; where a rule depends on them it says what it assumes.

## Summary of the forms

<!-- example: fragment: the 0.3.0 design; these forms do not compile yet -->
```neura
let json = import:Std.Encoding.Json;
let.promote _ = import:Std.Core.Arithmetic;

let Named = interface (
    let name: string;
        | get;
);

let User = record implements Named (
    id: int64,
    name: string,
    active: bool,
);

let Shape = class (
    export let label: string = "shape";
        | get.virtual: label;
);

let Circle = class: Shape (
    export let.override label: string;
        | get.override: "circle of " + label;
);

let sum = func<T: Additive<T>>(items: T[]) -> T {
    let total = T.zero();
    for (let item in items) {
        total = T.add(total, item);
    }
    return total;
};

let UserJson = derive json.Codec<User> {
    field User.name: "display_name";
};

let main = func() -> int {
    last user = new User(7, "Ada", true);
    last renamed = new with(user) { name: "Grace" };
    return renamed === user ? 1 : 0;
};
```

## Grammar

```text
record       = "record" [ "implements" type { "," type } ] "(" [ component { "," component } [ "," ] ] ")"
component    = { tag } name ":" type
copy         = "new" "with" "(" expression ")" "{" [ replacement { "," replacement } [ "," ] ] "}"
replacement  = name ":" expression

requirement  = [ "export" ] "let" name ":" type ";" bodyless { bodyless }        (interface member)
bodyless     = "|" ( "get" | "set" ) ";"
accessor     = "|" ( "get" | "set" ) [ "." ( "virtual" | "override" ) ] ":" body ";"
override     = "export" "let" "." "override" name ":" type ";" accessor { accessor }   (class member)

static-req   = [ "export" ] "static" "let" name "=" "func" [ "(" parameters ")" ] [ "->" type ] ";"   (interface member)
conformance  = "module" "implements" type { "," type } "(" members ")"           (trusted companions only)

derive       = "derive" qualified-name "<" type ">" [ "{" { mapping } "}" ]
mapping      = ( "field" | "variant" ) qualified-name ":" string ";"
```

`record` and `derive` are the value of a declaration, written where `class` is: `[export] let Name = record (...);` and `[export] let Name = derive G<T> {...};`. Both end with `;` like every declaration.

| Rule | Status |
| --- | --- |
| `record`, `with`, `derive`, `field`, `variant` and `virtual` are contextual. None becomes a keyword, and each stays an ordinary name elsewhere. | proposed |
| `record` begins a record only as the first token of the initializer of a `let` or `const` declaration that has no type annotation, and only when the tokens after it are `implements`, or `(` followed by any tags and then one of: a name and `:`; one of the keywords `export`, `let`, `const`, `static`; or the word `last` and a name. None of those begins an argument list today. Every other use of the name is an expression, so `let total = record();`, `let entry = record("B");`, `let small = record < limit;` and `record(first);` keep their meaning. | proposed |
| A record is declared wherever a class declaration is accepted today, and nowhere else. | proposed |
| `record<T> (...)` and `record: Base (...)` are therefore read as expressions and fail as such, at the first `:` (`E0202`) and at the `:` (`E0201`); when the initializer begins with `record <` or is the bare name `record`, the error carries the help `a record is not generic in this release` or `a record has no base class`. | proposed |
| After the M5 named arguments, `let x = record(count: limit);` reads as a record with one field. A call of a function named `record` whose first argument is named, written as the whole initializer of an unannotated declaration, is written `(record)(count: limit)`. | proposed |
| `new with` always begins copy construction. A class named `with` can no longer be constructed by `new with(...)`; no source in the repository does. The source expression is one expression in parentheses, the braces are required, and they may be empty. | proposed |
| `derive` begins a derivation only as the first token of a declaration's initializer and only when an identifier follows it. Two adjacent identifiers begin no expression, so no accepted program changes. | proposed |
| `field` and `variant` are words only at the start of a mapping inside a derivation's braces. A mapping's right side is one string literal: no concatenation, no constant name, no interpolation. | proposed |
| A clause belongs to the member whose `;` it follows. A clause before the first member of a class is not an accessor clause; the hooks milestone uses that position for class hooks, including its own `get.override` and `set.override`, and nothing in this note reads a clause there. | proposed |
| An accessor clause's word may be followed by `.virtual` or `.override`. The three tokens may be separated by spaces or comments; `neura fmt` writes them joined. A bodyless clause `\| get;` is accepted only on an interface property, and a clause with a body only in a class. | proposed |
| A record's components are not grouped (`record (x, y: float64)` is not a record), take no default value, and take no `export`, `let`, `last`, `const` or `static`. Tags and documentation comments before a component apply to its field. A trailing comma is allowed. | proposed |
| A record is not generic in 0.3.0, has no base, and its body holds components only: no methods, constructors, destructors, accessors, casts, nested types or static members. | proposed |
| The parser builds explicit forms: `Declaration::record`, `Expr::Kind::Copy`, `Declaration::property_requirement`, `Declaration::property_override`, `AccessorClause::dispatch` (`None`, `Virtual`, `Override`) and `AccessorClause::bodyless`, and `Declaration::Kind::Derive` with its mappings. A property requirement or an accessor-only override is never an uninitialized field. | proposed |

**Formatter.** `neura fmt` writes a record like an enum with named payloads: one component per line, indented once, with a trailing comma, or on one line when it was written on one line and fits. It keeps the comments between components. A copy is written `new with(source) { a: x, b: y }` on one line when it fits and one replacement per line with a trailing comma otherwise. A bodyless clause is indented under its property like an accessor clause. A derivation's mappings are one per line.

**Language server.** `record`, `with`, `derive`, `field`, `variant` and `virtual` are highlighted as keywords where they act as one. Completion offers a record's field names inside the braces of a copy (without the ones already written), the properties of the base inside `let.override`, the static requirements of a type parameter's bounds after `T.`, and `field Type.member` and `variant Enum.Variant` paths inside a derivation. Hover on a record shows its generated constructor and value members; hover on a derivation shows the generated functions and its schema. Go to definition on a replacement name, a mapping path, or an overridden property goes to the field. Rename is described under [Tooling](#tooling).

## Records

A record is a nominal class that is final and deeply immutable, with a generated constructor and generated value members.

| Rule | Status |
| --- | --- |
| `let User = record (id: int64, name: string);` declares the class `User` with one `export last` field per component, in order, and the constructor `export let init = func(id: int64, name: string) => set;`. The parser builds those members and expands the shorthand with `expand_constructor_shorthands`, so construction, definite assignment and `last` are the rules of [the constructor shorthand](Parameters030.md#constructor-shorthand). No other constructor exists and no path writes a field outside it. | proposed |
| A record is constructed like any class: `new User(7, "Ada")`, target-typed `new(7, "Ada")`, and with named arguments once M5 lands, whose labels are the field names. | proposed |
| A record behaves as a class tagged `#Final` and `#Immutable`. Extending it is `E0912`. Its instances are shared between tasks as those of an `#Immutable` class are. Writing either tag is allowed and changes nothing; `#Sealed` and `#NotThreadSafe` conflict as they do with the written tags. | proposed |
| A record is a reference type. `nil` is a value of every record type, `==` and `!=` compare identity, `is` and `as` work as for classes, and a record may be stored in `class`. | proposed |
| A record may implement interfaces whose requirements its fields, its generated value members or interface defaults satisfy: property requirements that need only `get`, `equals`, `hash_code`, `to_string`, and methods with default bodies. Any other requirement is unsatisfied (`E0504`). | proposed |
| A field's type must be *record-eligible* (table below); otherwise `E0571` at the component's type. | proposed |
| A record needs at least one component. A component may not be named `init`, `deinit`, `equals`, `hash_code`, `to_string` or `instance`. Two components with one name are `E0303`. | proposed |
| `destroy` of a record is an error (`E0514`): copies share their children, so the collector owns every record. | proposed |
| A record declares no class hook, and the hooks milestone gives it the identity hooks. No record field holds a class whose hooks could change the stored view, because the only class types a field may hold are records. | proposed |
| A record has no deconstruction protocol. `let [a, b] = user;` stays an error. | proposed |

| Field type | Record-eligible |
| --- | --- |
| `bool`, integers, floating-point types, `string` | Yes |
| A fieldless enum | Yes |
| A tuple, or a payload enum (including `Optional<T>`) | When every element or payload is. The inline limit (`E0449`) applies as it does to any field. |
| A record, including the record being declared | Yes |
| `Std.Collections.Frozen.FrozenList<T>` | When the frozen-collection work lands and `T` is eligible; that work adds the row. Until then it is a class like any other. |
| Any other class (also one tagged `#Immutable`), an interface, `class`, an array, a list, a function type, `&T`, `any`, a scope-bound type | No |

### Storage

A record is a heap object in every position: a local, a parameter, a result, a field, an element. It is never flattened into value words. Four rules force this: `==` on records is identity, so a copy must be a distinct object; `nil` is a record value; copy construction must be exactly one allocation that a summary can report; and value-struct classes are outside 0.3.0. Storage030 therefore sees a record as it sees any class: one reference word wherever the record is held, including inside a tuple, an enum payload or an aggregate element, and an object whose fields store their tuples and payload enums inline as value words. A record never holds an array, a primitive reference or a region entry, so it is never region-owned and no escape rule applies to it beyond those of a class reference.

Since every field is assigned once in the constructor and holds only immutable values, a record graph has no cycle. Its depth is unbounded (a record may hold its own type), so the generated value members recurse as deep as the data; exceeding the frame limit is the ordinary frame-depth trap.

### Value members

Lowering generates three overrides in every record. They are ordinary methods with bodies, exported, and present in NeuraIR.

| Member | Result | Status |
| --- | --- | --- |
| `equals(other: class) -> bool` | False when `other` is nil or is not this record type. Otherwise the fields are compared in declaration order with the value equality of each field's type, stopping at the first unequal pair. Two records of different declarations are never equal, whatever their fields. | proposed |
| `hash_code() -> uint32` | Start with 1; for each field in declaration order, 31 times the hash plus the field's hash, wrapping. The type does not enter the hash, and no identity number or image-local identifier does. | proposed |
| `to_string() -> string` | The qualified name, `(`, each field as `name: text` separated by `, `, `)`: `App.User(id: 7, name: Ada, active: true)`. The qualified name and field names are string constants of the function; they do not depend on the `NAME` or `SMAP` sections. | proposed |

Field equality, hash and text are those of [the standard library's table](../lang/Stdlib.md#52-equality-hashing-and-text): a float is equal by bit pattern with every NaN equal to every NaN and `0.0` unequal to `-0.0`, and hashes its canonical bits; a nil record field equals only nil, hashes to 0 and prints `nil`. `a === b` is `a.equals(b)`, so it traps when `a` is nil, as any method call does.

That table has no row for payload enums, and records hold them. This milestone adds it, for every payload enum whose payloads all have value members:

| Type | Equal when | Hash | Text | Status |
| --- | --- | --- | --- | --- |
| Payload enum | Same variant, and each pair of payloads equal, in payload order | Start with the variant's ordinal plus 1; for each payload, 31 times the hash plus the payload's hash, wrapping | The variant name; then for positional payloads `(`, the payloads' text separated by `, `, `)`; for named payloads `{`, each `name: text` separated by `, `, `}` | proposed |

`==` on a payload enum stays undefined. The converter generates these members where it generates a tuple's, reading the enum's words.

### Copy construction

| Rule | Status |
| --- | --- |
| `new with(source) { f: e, ... }` has the type of `source`, which must be a record type; otherwise `E0572`. A `nil` literal, a value of type `class`, of an interface type or of a type parameter is not a record type. The form is never target-typed. | proposed |
| Each replacement names a field of that record. An unknown name is `E0305`, a name written twice is `E0303` at the second, and a value not assignable to the field is `E0403`. | proposed |
| Order: evaluate `source` once; trap if it is nil, with the trap of a field read through nil, before any replacement runs and also when every field is replaced; evaluate and admit the replacements in written order; read the fields that have no replacement from the source, in declaration order, once each; call the generated constructor with the values in declaration order. | proposed |
| The copy is exactly one allocation, made by the constructor. Nothing is cloned: the copy shares every child reference with its source. | proposed |
| The expansion is the source events and no others: the read of `source`, each replacement's evaluation and admission as a constructor argument, each selected field read, and the constructor's field stores. Moving values between them adds no event. | proposed |
| `new with(source) { }` is accepted and yields a record that is `===` to its source and not `==`. | proposed |
| The allocation summary of the expression is one object of the record type, as for `new R(...)`. It needs no enclosing `new`, since it is one. | proposed |

## Interface properties

| Rule | Status |
| --- | --- |
| An interface member `let name: T;` followed by `\| get;`, or by `\| get;` and `\| set;` in either order, is a *property requirement*. It declares no storage. `get` is required; a requirement with `set` alone, with an initializer, with a clause body, with `static`, `const` or `last`, or with a repeated clause is an error (`E0573`, repeated clause `E0520`). | proposed |
| A class satisfies a `get` requirement with an exported instance field of the same name and exactly the same type, its own or inherited, that code outside the class may read: a field without accessors or one with a `get` clause. It satisfies `set` with one that outside code may write: a `let` field without accessors, or one with a `set` clause. A writable field satisfies a requirement that needs only `get`. No variance applies. Anything else is `E0504` at the class, naming the property and what is missing. | proposed |
| Requirements of one name from several interfaces must have the same type (`E0504` otherwise) and are joined: the class must supply `set` when any of them asks for it. | proposed |
| A property of a generic interface may use the interface's type parameters. | proposed |
| Reading `value.name` through an interface-typed receiver, or a receiver whose type is a type parameter bounded by the interface, evaluates the receiver once and dispatches to the getter of the receiver's class. Writing dispatches to its setter. A write when the requirement has no `set` is `E0510`. A compound assignment, `++` and `--` evaluate the receiver once, call the getter, evaluate the right operand, compute with the property's type (a checked-arithmetic trap comes before the setter), and call the setter. | proposed |
| A property has no addressable storage: `&` of it is `E0524`. A property that is read-only through the interface promises neither purity, nor immutability, nor equal results on two reads. | proposed |
| A getter or setter reached through an interface runs on a nil receiver as any method does: it traps. | proposed |

**Lowering.** A requirement becomes one or two bodyless functions of the interface, `$get_<name>() -> T` and `$set_<name>(value: T)`, each carrying the attribute `property.requirement`. They are the names the class's accessors already have, so an interface read is a `VirtualCall` of `$get_<name>` and reaches a field's existing hidden getter by the runtime's existing rule (the method of that name in the receiver's class, then its bases). For a field that has no clause of the needed kind, lowering generates an *adapter*: a hidden method of that name that reads or stores the field as outside code would, marked `accessor.of` and `generated`. It is generated in each class that implements the interface and has no method of that name in itself or its bases. An adapter is generated only for a class that implements an interface with that requirement, so no other program gains a function. Access through a class-typed receiver is unchanged: a direct field read, or the static call it is today.

The hooks milestone defines the admission events of a class-valued property; this note assumes only that an adapter's read and store are the field's logical read and store and add no event of their own.

## Virtual accessors

| Rule | Status |
| --- | --- |
| `\| get.virtual: body;` and `\| set.virtual: body;` are accessor clauses that also establish a *slot*. The field keeps its storage and every rule of [accessors](../lang/Types.md#accessors). An unmarked clause keeps its static behavior exactly. | proposed |
| Outside access to a field through a receiver of the declaring class or a subclass dispatches a clause that has a slot on the receiver's class, and calls an unmarked clause statically as today. The two clauses of one field are independent. | proposed |
| A subclass overrides with `export let.override name: T;` followed by `\| get.override: body;`, `\| set.override: body;` or both. The declaration names the inherited property; it declares no field, takes no initializer, and repeats the property's type exactly. A clause it does not replace is inherited. | proposed |
| Each `.override` clause needs a slot of that kind, virtual or already overridden, in a base. An override continues the slot, so a further subclass may override again. | proposed |
| Errors (`E0575`): no inherited exported field of that name; no slot for the clause; a different type; an initializer; no clause at all; a plain or `.virtual` clause under `let.override`; `.override` on a clause of an ordinary field; a `let.override` property without `export`. `.virtual` on a clause in an interface, or combined with `.override`, is `E0574`. | proposed |
| Inside an override clause of property `p`, the bare name `p` and `instance.p` call the inherited accessor of the nearest base statically: a read calls its getter, a store its setter (`E0510` or `E0515` when the base has none). They never reach the storage, which stays private to the declaring class, and they never dispatch, so `\| get.override: "circle of " + label;` does not call itself. In a setter override, `value` is the incoming value. | proposed |
| Everywhere else in the subclass the inherited name dispatches, as an outside access does. | proposed |
| The evaluation order of plain and compound access is that of accessors today. A virtual accessor satisfies an interface property requirement like any other accessor; interface dispatch then reaches the most derived override. | proposed |

**Lowering.** A slot's accessor is the existing hidden method with the attribute `accessor.virtual`. An override is a hidden method of the same name in the subclass, linked to the one it overrides by the existing override link and to the inherited field by `accessor.of`. A dispatched access is a `VirtualCall` in place of today's `Call`. No field is added to any layout.

## Static interface requirements

| Rule | Status |
| --- | --- |
| A static function without a body is a *static requirement* when it is a member of an interface. It may use the interface's type parameters, unlike other static members. Elsewhere a static function still needs a body (`E0501`). | proposed |
| A requirement is not generic itself, not variadic, not an overload group, has no parameter default, and has no host, kernel or scheduler binding (`E0576`). An interface has no static requirement for a field. | proposed |
| A class that lists the interface after `implements`, directly or through an interface that extends it, must itself declare an exported static function of that name with exactly the requirement's parameter types and result type after substituting the interface's type arguments; otherwise `E0504`, naming the requirement. An inherited static function does not implement it. A subclass inherits the conformance with its base's functions. Parameter names must agree, under the calls milestone's rule for the names of an overriding method. An overload group, a generic function or a variadic one does not implement a requirement. | proposed |
| A generic class cannot implement a requirement that mentions its own type parameters, because its static members cannot use them. That is today's `E0501`. | proposed |
| Inside a generic body, `T.name(arguments)` calls the requirement `name` found in the bounds of the type parameter `T`. A name no bound has is `E0305`; a name two bounds have with different signatures is `E0577`; equal signatures are one requirement. Arguments are evaluated and admitted in order as for any call. | proposed |
| A requirement is called only through a type parameter or through an implementing class's own name (an ordinary static call). `Additive<int>.zero()`, and `value.zero()` on an interface-typed value, are `E0576`: there is no dispatch on a run-time value. | proposed |
| Specialization replaces every such call with a direct call of the implementation for the type argument. Nothing remains to resolve at run time, and nothing dispatches through `any`. | proposed |
| A requirement adds no storage, per specialization or otherwise. A generic class still shares its static members across type arguments. | proposed |
| A contract's name promises nothing. An implementation may trap, allocate, run its class's static initializer, or be unsafe to call from a task; each specialization has the effects of the implementation it calls, and `#Pure` and task safety are checked on the specialized program. No pass reassociates, reorders or parallelizes calls because the requirement is named `add`. | proposed |
| An interface with static requirements is still an interface type. A class instance may be stored in it; a primitive may not. | proposed |

### Primitive conformance and the contracts

A primitive type conforms to an interface only through its trusted companion module. `module implements I<...>, ... (` is accepted only on a companion in the trusted standard library root (`E0578` elsewhere), and only for a *static contract*: an interface, with any parents, that has static requirements and nothing else. The companion's exported functions implement the requirements under the rule for classes, and the primitive then satisfies a bound of that interface instance. No type conforms by having functions of the right names.

`Std.Core.Arithmetic` (new file `stdlib/std/core/arithmetic.nra`) declares four contracts:

| Contract | Static requirements | Conforming primitives | Status |
| --- | --- | --- | --- |
| `Additive<T>` | `zero() -> T`, `add(left: T, right: T) -> T` | Every sized integer and float | proposed |
| `Multiplicative<T>` | `one() -> T`, `multiply(left: T, right: T) -> T` | Every sized integer and float | proposed |
| `Ordered<T>` | `compare(a: T, b: T) -> Ordering` | Every sized integer, `bool` (the companions' existing `compare`) | proposed |
| `Parseable<T>` | `parse(text: string) -> Result<T>` | Every sized integer and float, `bool` (the companions' existing `parse`) | proposed |

The new companion functions are the operators: `add` is `left + right` and `multiply` is `left * right` at the type's width, with checked overflow for integers and the specified rounding for floats. Floats do not conform to `Ordered` in 0.3.0, since their companions have no total `compare`. `int` and `float` are aliases and use the companion of the type they name.

## Derivation

| Rule | Status |
| --- | --- |
| `let Name = derive G<T>;` or `... { mappings };` asks a compiler-owned generator for a module named `Name`. `G` must resolve to a generator the compiler owns; the only one in 0.3.0 is `Std.Encoding.Json.Codec`, declared in the trusted library. Any other target is `E0579`. | proposed |
| A derivation is a member of a file or a module. It is not written in a function body, a class or an interface, and is not an expression. `export` exports the module. | proposed |
| The result is a module of ordinary checked functions. It is not an object: nothing is allocated to hold a codec, and `Name` is used as a module is (`UserJson.encode(user)`). | proposed |
| Tags never cause generation. `#Serializable`, `#SerialName` and `#Skip` stay reserved and are not read by a derivation. | proposed |
| A mapping `field Q.member: "wire";` pins the wire name of a record field, or of a named payload field written `Enum.Variant.field`; `variant Enum.Variant: "wire";` pins a variant's. `Q` is resolved like any type name. The member must exist, be accessible, and belong to a shape reachable from `T` (`E0318`). Two mappings for one member, an empty wire name, and two members of one record, one enum or one named payload with the same wire name after mapping are `E0319`. A mapping holds for every occurrence of the shape in this derivation and for no other derivation. | proposed |
| Two derivations for one type are two independent modules. | proposed |

### The JSON codec

`Std.Encoding.Json.Codec<T>` is declared in `stdlib/std/encoding/json.nra` as a static contract, which documents and checks the generated module's functions:

| Function | Meaning | Status |
| --- | --- | --- |
| `to_json(value: T) -> Result<JsonValue>` | The value as a JSON tree | proposed |
| `encode(value: T) -> Result<string>` | `Json.write` of `to_json` | proposed |
| `from_json(value: JsonValue) -> Result<T>` | The value a JSON tree describes | proposed |
| `decode(text: string) -> Result<T>` | `Json.parse`, then `from_json` | proposed |

`T`, the root, is a record or an enum other than `Optional`. The shapes a codec supports, each checked over everything reachable from the root (`E0590` at the derivation, naming the path to the unsupported type):

| Shape | JSON | Status |
| --- | --- | --- |
| `bool` | `true`, `false` | proposed |
| Integers of every width | A number written as an integer, exactly. Decoding accepts only a number with no fraction and no exponent that the type holds; it goes through the type's `parse`, never through a float. | proposed |
| Floats of every width | The shortest round-trip text. Encoding a NaN or an infinity returns an error; decoding a number that rounds to an infinity does too. | proposed |
| `string` | A string | proposed |
| Record | An object with one member per field, in declaration order. A nil record is `null`. | proposed |
| Tuple | An array of exactly the tuple's length | proposed |
| Enum, fieldless or not | An object whose `"variant"` is the variant's wire name. A fieldless variant has no `"payload"`; positional payloads are a `"payload"` array in order; named payloads are a `"payload"` object. | proposed |
| `Optional<U>` as the type of a record field | `None` omits the member; a missing member decodes as `None`; a present member decodes as `Some`. A present `null` is `Some(nil)` when `U` is a record and an error otherwise. | proposed |
| `Optional` anywhere else (a tuple element, an enum payload, inside another `Optional`, the root) | Rejected (`E0590`) | proposed |
| `FrozenList<U>` | An array, when the frozen-collection work lands with its element and size limits. Until then rejected. | proposed |
| A shape that can reach itself (a record holding its own type, directly or through others) | Rejected (`E0590`): the first generator accepts only acyclic shapes. | proposed |
| Anything not record-eligible | Rejected (`E0590`) | proposed |

Schema rules:

| Rule | Status |
| --- | --- |
| A wire name is the declared name of the field or variant unless a mapping pins it. Renaming a declaration changes an unpinned wire name; the compiler keeps no memory of an earlier schema. | proposed |
| Encoding reads each field once, in declaration order, as outside code reads it, and emits members in that order. | proposed |
| Decoding an object: a member whose name is no field's is ignored; a field's name appearing twice is an error at the second; then, in declaration order, a missing required field is an error and each present field is decoded; then the record is built by its generated constructor, once. No field is written any other way. | proposed |
| Decoding an enum: `"variant"` must be present once and name a variant; `"payload"` must be absent for a fieldless variant and present once, with the right kind, length and member names, for the others. Unknown members of a named payload are ignored as for a record; unknown members beside `"variant"` and `"payload"` are ignored. | proposed |
| Expected failures are returned: a parse failure as `Json.parse` returns it, and every other one as `Error.custom(code, message)` with one of the codes `json.codec.type`, `json.codec.missing`, `json.codec.duplicate`, `json.codec.range`, `json.codec.variant`, `json.codec.payload`, `json.codec.not_finite`, `json.codec.too_large`, and a message that begins with the JSON path of the value, such as `$.owner.id`. The first failure in the order above is the one returned. A trap in a constructor or a hook is not caught. | proposed |
| Bounds: `decode` returns `json.codec.too_large` for a text longer than 16,777,216 bytes, before parsing. Nesting is bounded by `Json.MaxDepth` and by the shape, which is finite because it is acyclic. The number of objects a decode constructs is at most the number of record positions in the shape. | proposed |
| Wire names, error codes and paths are string constants of the generated functions. They are execution data and survive removal of `NAME` and `SMAP`. No type, layout or symbol identifier of an image appears in the JSON. | proposed |
| The generated functions allocate with ordinary constructor and list operations, and their summaries report it. | proposed |

**Expansion.** The generator runs in lowering, after declarations are resolved and before bodies are finalized. It builds a `Declaration` tree for the module, in which every type and callee is a `$`-prefixed name that no source can write and that lowering binds to the resolved symbol for this derivation only; the tree then goes through the ordinary lowering path, so visibility, construction, definite assignment and every other check apply to it. The module has the four functions and one private pair of helpers per reachable record and enum. Every generated node carries the span of the derivation, or of the mapping that named it, and every generated symbol carries `generated`. `neura check --show-derived[=Name] file.nra` prints the tree as Neura source with the bound names written as qualified names; the language server shows the same text.

## Lowering and artifacts

| Rule | Status |
| --- | --- |
| A record is a `Class` symbol with the attribute `class.record`; its fields, constructor and value members are ordinary symbols and bodies. A program that imports it from a `.nir` file finds the same members, and may copy-construct it. | proposed |
| A copy lowers to a scoped value: the source in a temporary, a nil check that fails as a nil field read does, the replacements, the field reads, and `Allocate` with the constructor. No new operation. | proposed |
| `T.name(...)` lowers to the new operation `ConformanceCall`: its symbol is the requirement, its first type argument is the type the call goes through, and its operands are the arguments. It is the one NeuraIR format change of the milestone and is written under the next free NeuraIR minor version, which the lead assigns when the step merges (the reader is at 1.10 as this note is written). A file without the operation keeps its version and bytes. An earlier reader rejects the newer version. | proposed |
| The verifier accepts `ConformanceCall` only before specialization, only for a bodyless static function of an interface, and only with a type argument that is a type parameter bounded by that interface or a type that conforms. After specialization none remains; one that does is `E0899`. | proposed |
| Specialization resolves a `ConformanceCall` by `static.implements`: the function that carries the attribute for that requirement's declaration and for the type, or for the nearest base of the type that has one. Lowering attaches it to every implementation when it checks a class or a companion, so a library compiled to `.nir` carries its conformances. A bound satisfied by a primitive makes lowering load the companion, by the mechanism that loads it for a primitive-qualified call. | proposed |
| Analyses that run before specialization (purity, task safety, effect and retention summaries) treat a `ConformanceCall` as they treat a value member of a type parameter: deferred, and decided on each specialization. `DispatchTargets` and `Converter::implementation` find a function by its owner and name, hidden accessors included, so a `VirtualCall` of a getter has every override and adapter as a target and the converter builds its dispatch block unchanged. | proposed |
| New symbol attributes, which need no format version; each takes the next free identifier in `attributes.def` when its step merges (the calls milestone takes identifiers of its own, so none is fixed here): `class.record` on a class (`s`, the generated constructor); `property.requirement` on a function (`[t u1]`, the property's name and 0 for get or 1 for set); `accessor.virtual` on a function (`b`); `static.implements` on a function, repeatable (`[s y]`, the requirement and the class or primitive whose declaration lists the conformance); `generated` on a function, class or module (`[t p]`, the generator, one of `record`, `property.adapter`, `derive:json.codec`, and the span of the declaration that caused it). | proposed |
| The verifier checks each: `class.record` names an `init` of that class and every field is `binding.last`; `property.requirement` only on a bodyless `$get_`/`$set_` function of an interface with a matching signature; `accessor.virtual` only on a function with `accessor.of`; `static.implements` names a bodyless static function of an interface and a function of the same substituted signature. | proposed |
| No NVR change and no new runtime operation. Records are objects, property and virtual-accessor access is the existing dispatch block, static requirements disappear in specialization, and wire names are string constants. The loader, the decompiler and the assembler need no change. | proposed |
| Tiers 2 and 3 decline nothing new. A dispatched accessor is a dispatch site like any other, so tier 3's guarded inlining applies to it under its existing limits: the tags recorded at the site select at most 6 callees of at most 8 blocks each that have no helper, no call left after inlining and no frame write. A generated field adapter only reads or writes one field, so it is expected to meet them, and a user's accessor meets them or not by its body; the gate measures it through the `tier: native programs` line and promises no particular site. A deoptimization inside an inlined accessor materializes its frame, so steps, traps and callers are those of tier 0. | proposed |
| The collector and the ABI are unchanged: a record is traced as an object, and a property of a multiword type returns its words as any function does (NVR 1.7). | proposed |
| The decompiler shows a record as a class with its generated methods, an adapter as a method, and a derived module as a module. With names in the image it uses them; it does not claim to recover `record`, `new with` or `derive` spellings, which lower as their written-out forms do. | proposed |

Where the work goes. The lowering lane has one writer, so each feature lives in its own file with a few call sites in `lowering.cpp`:

| Area | Files and functions |
| --- | --- |
| Syntax | `src/parser/syntax.hpp` (`Declaration`, `AccessorClause`, `Expr::Kind`); `src/parser/parser.cpp`: `declaration` (the `record` and `derive` branches beside `class`), `new_expression` (`with`), `accessor_clauses` and `skip_accessor_clauses` (`.virtual`, `.override`, bodyless), `recover` |
| Records | new `src/neura-ir/lowering_records.inc`: eligibility, value members, copy construction; called from `Lowering::declare`, `Lowering::lower_declaration` and `Lowering::expression_node`; `Lowering::check_constructor_shorthand` unchanged; `src/neura-ir/tags.cpp` for the implied `#Final` and `#Immutable` |
| Payload-enum value members | `src/neura-ir/aggregate.hpp` (`supports_value_members`), `src/neura-ir/dispatch.cpp` (`DispatchTargets::value_members`), the value functions of `src/backend/converter.cpp` |
| Properties and virtual accessors | `src/neura-ir/lowering_accessors.inc`: `check_accessor_member`, `declare_accessors`, `accessor`, `lower_accessors`, `route_field`, `read_place`, `store_place`; `Lowering::check_interfaces` and the interface-member check in `Lowering::lower_declaration`; `Lowering::check_override` |
| Static requirements | `Lowering::resolve` (the body rule for static functions), `Lowering::find` (type parameters in a static requirement), `Lowering::select` and `Lowering::call` (`T.name`), `Lowering::check_interfaces`, `Lowering::type_arguments` (the bound check, `E0418`, for a primitive); `src/neura-ir/ir.hpp`, `nir_format.cpp`, `ir_print.cpp`, `verifier.cpp`, `linker.cpp`, `specialize.cpp`, `purity.cpp`, `task_analysis.cpp`, `src/ir-optimization/effects.cpp` |
| Derivation | new `src/neura-ir/lowering_derive.inc` and `src/neura-ir/derive_json.cpp` (the tree builder and its printer); `stdlib/std/encoding/json.nra` (`Codec`) |
| Library | new `stdlib/std/core/arithmetic.nra`; the companions under `stdlib/std/core/` |
| Tooling | `src/tooling/formatter.cpp`, `analysis.cpp`, `lsp.cpp`, `refactor.cpp`; `src/cli/source_docs.cpp`; `src/cli/main.cpp` (`--show-derived`) |

## Tooling

| Rule | Status |
| --- | --- |
| Renaming a record field renames its component, the constructor's label, every replacement name in a copy, every use, and every mapping path that names it. A mapping's wire string is never changed. | proposed |
| When the renamed field or variant is reachable from a derivation that does not pin it, the rename also inserts a mapping that pins the old name, so the schema does not change. The inserted mapping is a separately annotated edit where the client supports change annotations, so the author can leave it out; `neura` refactoring from the command line inserts it and says so. | proposed |
| Renaming a property renames the interface requirement, every satisfying field and every `let.override` of it together, or is refused when one of them is outside the workspace. | proposed |
| The documentation index lists a record with its components and generated constructor, a property requirement with `get` or `get/set`, a static requirement as a static function of the interface, and a derived module with its four functions and its wire names. | proposed |
| The debugger shows a record's fields raw. It never calls a getter, an adapter or `to_string` to display a value, and it steps over functions marked `generated` unless asked to step into them. | proposed |

## Diagnostics

Codes of this milestone: `E0570` to `E0579`, `E0318`, `E0319` and `E0590`. Syntax errors use the parser's existing codes; the milestone has no parser code of its own.

| Code | Title | Span | Message and fix |
| --- | --- | --- | --- |
| `E0570` | invalid record declaration | The offending token or component | One message per cause: `a record component has no default value`, `a record component takes no 'export'` (or `let`, `last`, `const`, `static`), `a record needs at least one component`, `a record component cannot be named 'init'` (and the other reserved names), `a record holds components only`. Fix: remove the part, or write a class. |
| `E0571` | record field of a type a record cannot hold | The component's type | `field 'tags' has type 'string[list]', which a record cannot hold`, with a label naming the rule (mutable, not a record, no value equality) and help: hold a record, a tuple or an enum of eligible types, or declare the type as an `#Immutable` class and not as a record. |
| `E0572` | copy construction of a value that is not a record | The source expression | `'new with' copies a record, and this is 'Shape'` (a class, an interface, `class`, nil, a type parameter). Fix: construct the class with `new`. |
| `E0573` | invalid interface property | The clause or the property | `an interface property needs '| get;'`, `an interface property has no initializer`, `a property clause in an interface has no body`, `an interface property is declared with 'let'`. Suggestion: insert `| get;`, or delete the body. |
| `E0574` | invalid virtual accessor | The clause's word | `'.virtual' has no meaning in an interface`, `a clause is '.virtual' or '.override', not both`. |
| `E0575` | invalid accessor override | The property name or the clause | `'label' overrides no property of 'Shape'`, `'Shape.label' has no virtual getter to override` with a label at the base clause and help `mark it '| get.virtual:'`, `an accessor override repeats the property's type 'string'`, `an accessor override has no initializer`, `an accessor override needs a '.override' clause`, `'.override' needs 'let.override'`. |
| `E0576` | invalid static requirement or call of one | The declaration, or the qualifier of the call | `a static requirement is not generic` (variadic, overloaded, bound), `a static requirement is called through a type parameter or an implementing class`. |
| `E0577` | ambiguous static requirement | The member name in `T.name` | `'zero' is required by 'Additive<T>' and by 'Monoid<T>' with different signatures`, labels at both. Fix: call through one bound only, or rename a requirement. |
| `E0578` | conformance declared by a module | `implements` | `only a primitive's companion in the standard library declares 'implements'`, or `'I' has instance members, so a primitive cannot conform to it`. |
| `E0579` | unknown derivation | The generator's name | `'App.Codec' is not a generator; this release derives 'Std.Encoding.Json.Codec<T>'`. A derivation in a function body or class: `a derivation is a member of a file or module`. |
| `E0318` | derivation mapping names no member of the derived shape | The mapping's path | `'User.nick' is not a field of 'User'`, or `'Audit' is not reachable from 'User'`. Suggestion: the closest field name. |
| `E0319` | duplicate derivation mapping or wire name | The second mapping, or the derivation with labels at both members | `'User.name' is mapped twice`, `'id' and 'key' of 'User' both have the wire name "id"`, `a wire name is not empty`. |
| `E0590` | type that a derived codec does not support | The derivation's type argument | `'Json.Codec' cannot encode 'Team': Team.lead: User -> User.manager: User reaches itself`, or names the `Optional` position or the ineligible type, with the path from the root. |

Reused codes, with the message this milestone adds:

| Code | Use |
| --- | --- |
| `E0202` | A malformed record, copy, clause or derivation: `expected ':' and a type after the component name`, `expected '{' after 'new with(...)'`, `expected 'field' or 'variant'`, `expected a string literal for the wire name`, `expected ':' after 'get'` for a bodyless clause in a class. |
| `E0303`, `E0305`, `E0403` | A duplicate component or replacement; an unknown field in a copy or an unknown `T.name`; a replacement of the wrong type. |
| `E0304` | An unknown name in `let x = record(a, b: T);` gains the help `a record's components are not grouped: write 'a: T, b: T'`. |
| `E0417`, `E0418` | A derivation with the wrong number of type arguments; a type argument that does not satisfy a static-contract bound, naming the missing conformance. |
| `E0501` | A bodyless static function outside an interface (unchanged). |
| `E0504` | A property or static requirement the class does not satisfy: `'Rank' must have an exported field 'name: string' that can be read`, `... that can be assigned`, `'Money' must declare 'static let zero = func() -> Money'`. |
| `E0510`, `E0515`, `E0520`, `E0524` | A write to a get-only property; a missing inherited accessor inside an override; a repeated clause; `&` of a property. |
| `E0514`, `E0912` | `destroy` of a record; a class that extends a record. |
| `E0449`, `E0907` | A record field over the inline limit; `#Sealed` or `#NotThreadSafe` on a record. |

## Edge cases

Each is a test with the expected result. *Accept* names what the program does; *reject* names the code and the span.

Records:

1. `let P = record (x: int, y: int);` and `let P = record(x: int, y: int,);`: accept; the trailing comma changes nothing.
2. A record split over lines with a `//` comment and a documentation comment between components: accept; the documentation attaches to the next component.
3. `let P = record ();`: the tokens are a call of `record` with no arguments; reject `E0304` when no `record` is in scope, with the help `a record needs at least one component`.
4. `let P = record implements Named ();`: reject `E0570`, at `)`.
5. `let P = record (x, y: float64);`: a call by the lookahead; reject `E0304` with the grouping help.
6. `let P = record (x: int = 0);`: reject `E0570` at `=`.
7. `let P = record (export x: int);`, `(last x: int)`, `(let x: int)`, `(static x: int)`: records by the lookahead; reject `E0570` at the modifier.
8. `let P = record<T> (x: T);`: an expression by the lookahead; reject `E0202` at the first `:`, with the help that a record is not generic in this release.
9. `let P = record: Base (x: int);`: the initializer is the name `record`; reject `E0201` at `:`, with the help that a record has no base class.
10. `let P = record (x: int; export let f = func() -> int => x;);`: reject `E0202` at `;`, with help that a record holds components only.
11. `let P: class = record (x: int);`: the annotation makes the initializer an expression, a call of `record` with a named argument; reject `E0304` when no `record` is in scope.
12. `let total = record();`, `let e = record("B");`, `record(first);`, `return record(second);` with a function `record` in scope: accept; calls, unchanged.
13. `let x = record(count: limit);` with a function `record` in scope: parsed as a record; reject `E0421` at `limit`, which names a value and not a type, with the help to write `(record)(...)`. `let x = (record)(count: limit);` accept.
14. `let record = 1; let r: int = record;`: accept; a name.
15. `#Since("0.3.0") let P = record (#Deprecated("use b") a: int, b: int);`: accept; tags on the record and on a field.
16. `#Immutable #Final let P = record (x: int);`: accept. `#Sealed let P = record (...)`: reject `E0907`.
17. `let P = record (x: int, x: int);`: reject `E0303` at the second `x`.
18. `let P = record (init: int);`: reject `E0570` at the name.
19. `let P = record (items: int[list]);`, `(f: func() -> int)`, `(r: &int)`, `(c: class)`, `(n: Named)`: reject `E0571` at each type.
20. `let P = record (m: Money);` with `Money` an `#Immutable` class: reject `E0571`, with help to make `Money` a record.
21. `let Node = record (value: int, next: Node);`: accept; `new Node(1, nil)` and a chain of them.
22. `let P = record (pair: [int, string], state: Optional<Node>, mode: Mode);`: accept.
23. `let P = record (cell: Optional<int[list]>);`: reject `E0571` at the type, naming the list inside.
24. `let Q = class: P ();` with `P` a record: reject `E0912`.
25. `new P(1, 2)`, `let p: P = new(1, 2);`, `new P(y: 2, x: 1)` (M5): accept; fields as given.
26. `new P(1)` and `new P()`: reject `E0407`.
27. `p.x = 3;`: reject `E0445`; a record has no place where a field can be assigned again.
28. `destroy p;`: reject `E0514`.
29. `let [a, b] = p;`: reject, as for any class today.
30. `new P(1, 2) == new P(1, 2)` is false; `===` is true; `p == p` is true.
31. Two records with the same components: `new A(1) === new B(1)` is false, and so is `a.equals(nil)`. `nil_record === p` traps (nil receiver).
32. A `float64` field: NaN equals NaN under `===`; `0.0` and `-0.0` differ; hashes agree for equal values and differ for the zeros.
33. `hash_code` of `P(1, 2)` is `31 * (31 * 1 + 1) + 2`; equal records hash equally in every build, optimized or not, from source or from `.nir`.
34. `to_string` of a nested record with a nil field: `App.Node(value: 1, next: nil)`; the same text in an image stripped of names.
35. A record as a `HashMap` key through its value members: accept; two equal records are one key.
36. A record passed to `spawn` and sent on a channel: accept; the same object arrives.
37. A record implementing an interface with a method that has no default: reject `E0504`.
38. A record imported from a `.nir` file: construct, copy, compare; accept, same results.
39. A record declared inside a module and as a `static let` of a class: accept. `let p: record (x: int) = q;`, a record written in a type position: reject `E0202` at `(`.
40. A payload enum in `===` outside a record: accept under the new row; `==` on it still reject `E0413`.

Copy construction:

41. `new with(user) { name: "Grace" }`: accept; one allocation, `id` and `active` shared, `renamed != user`.
42. `new with(user) { }` and `new with(user) {}`: accept; `===` and not `==`.
43. `new with(user) { name: "G", }`: accept. `{ , }` and `{ name: "G",, }`: reject `E0202`.
44. `new with(user)` with no braces, and `new with(user);`: reject `E0202` at the token after `)`.
45. `new with user { ... }`, `new with() { }`, `new with(a, b) { }`: reject `E0202`.
46. `new with(user) { nick: "x" }`: reject `E0305` at `nick`, suggesting `name`.
47. `new with(user) { name: "a", name: "b" }`: reject `E0303` at the second.
48. `new with(user) { id: "seven" }`: reject `E0403` at the value.
49. `new with(user) { name }` and `{ owner.name: "x" }`: reject `E0202`.
50. `new with(nil) { }`, `new with(shape) { }` with a class, `new with(named) { }` with an interface value, `new with(t) { }` with `t: T`: reject `E0572`.
51. `let u: User = nil; new with(u) { id: f() }`: traps before `f` runs. With every field replaced: still traps.
52. `new with(make()) { name: g(), id: h() }`: `make` once, then `g`, then `h`; then `active` is read; the constructor receives `h`'s, `g`'s and the read value in declaration order. A trap in `g` prevents `h`.
53. `new with(new with(u) { id: 1 }) { name: "x" }`: accept; two allocations.
54. `new with(u) { next: new with(u) { } }` on a record holding its own type: accept.
55. `let v: User = new with(u) { };` and `let s: Shape = new with(u) { };`: accept the first; reject `E0403` for the second.
56. A comment between `new` and `with`, and a line break before `{`: accept.
57. `new with(u) {` at the end of the file in the editor: one `E0203`; the recovering parser keeps the source expression, and completion offers the field names.
58. `let with = 3; let w = with + 1;`: accept; a name. `new with(3)` with a class `with`: reject `E0202` (no braces).

Interface properties:

59. `let name: string;` then `| get;` in an interface: accept. Then `| get;` and `| set;`, in either order: accept.
60. `export let name: string; | get;`: accept; `export` is implied and may be written.
61. `let name: string;` with no clause in an interface: reject `E0573` at the name, suggesting `| get;`.
62. `| set;` alone: reject `E0573`. `| get; | get;`: reject `E0520`.
63. `let name: string = "x"; | get;`, `last name: string; | get;`, `static let name: string; | get;`: reject `E0573`.
64. `| get: "x";` in an interface: reject `E0573` at the body. `| get;` on a class field: reject `E0202` (expected `:`).
65. A class with `export let name: string;` and no accessors implements a `get` requirement and a `get`/`set` one: accept; an adapter is generated only then.
66. A class with `export last name: string;` implements `get`: accept; implements `get`/`set`: reject `E0504`.
67. A class whose field has only `| get: ...;` implements `get`: accept, and the interface read runs that getter once. With only `| set: ...;`: reject `E0504`.
68. A field `name: int` for a requirement `name: string`, a field that is not exported, a static field, and a method named `name`: reject `E0504`.
69. The field is inherited from a base that does not implement the interface: accept; the adapter is in the subclass, and no base layout changes.
70. Two interfaces require `name: string`, one with `set`: the class needs a writable field. With different types: reject `E0504`.
71. `value.name = "x"` through an interface with `get` only: reject `E0510`. `let r: &string = &value.name;`: reject `E0524`.
72. `value.count += f()` through an interface: receiver once, getter, `f`, checked add (a trap before the setter), setter.
73. A getter with a side effect read through the interface twice: runs twice.
74. `func<T: Named>(v: T) -> string => v.name`: accept; dispatches, then calls directly in each specialization.
75. A nil interface reference: `value.name` traps.
76. A generic interface `Holder<T>` with `let item: T; | get;` implemented by `class implements Holder<int>` with `export let item: int;`: accept.
77. A record implementing `Named`: accept; implementing an interface whose property needs `set`: reject `E0504`.

Virtual accessors:

78. `| get.virtual: label;` on a field, a subclass with `export let.override label: string;` and `| get.override: ...;`: accept; a read through a `Shape` reference to a `Circle` runs the override.
79. The override body reads `label`: the base getter runs, statically; no recursion.
80. Without `.virtual` in the base: reject `E0575` at `get.override`, with the help to mark the base clause.
81. `get.virtual` and `set.virtual` on one field; the subclass overrides only the setter: the getter is inherited.
82. `let.override label: int;` for a `string` property: reject `E0575`. `let.override label: string = "x";`: reject `E0575`.
83. `let.override label: string;` with no clause, or with `| get: ...;`: reject `E0575`.
84. `| get.override: ...;` on an ordinary field, and `let.override missing: string;`: reject `E0575`.
85. `| get.virtual.override: x;`: reject `E0574`. `| get.final: x;`: reject `E0202`.
86. `| get . virtual : x;` with spaces and a comment between the tokens: accept; formatted `get.virtual:`.
87. A third class overriding the override: accept; the most derived runs.
88. A subclass method reading the inherited `label` outside an override clause: dispatches.
89. The subclass has no field `label` in its layout; both objects have the same number of fields as before the feature.
90. A base with an unmarked `| get:` read through a subclass: the static call of today, image unchanged.
91. `shape.label += "!"` with virtual get and set: receiver once, dispatched getter, concatenation, dispatched setter.
92. A virtual getter satisfying an interface requirement, overridden in a subclass: the interface read reaches the override.
93. `&shape.label`: reject `E0524`, as today.
94. `let.override` of a property of a `#Final` class's base, and `.virtual` in a `#Final` class: accept; nothing can override the second.

Static requirements:

95. `static let zero = func() -> T;` in `interface<T>`: accept. The same in a class: reject `E0501`.
96. `static let make = func<U>() -> U;`, a variadic one, an `or func` group, and `static let limit: int;` without a value in an interface: reject `E0576` (the last stays `E0501`).
97. `class implements Additive<Money>` with both static functions: accept; `sum<Money>(items)` calls them directly.
98. A missing `zero`, a `zero` that is not static, not exported, or returns another type: reject `E0504`.
99. `class Money: Base implements Additive<Money>` with `zero` and `add` declared only in `Base`: reject `E0504`. `Gold: Money` as the type argument of `func<T: Additive<Money>>`: accept; `T.zero()` calls `Money.zero`.
100. `sum<string>(items)`: reject `E0418`, naming `Additive<string>`.
101. `sum<int8>`, `sum<int64>`, `sum<uint64>`: accept; overflow traps at the width, at the same element in optimized and unoptimized builds.
102. `sum<float64>` over `[0.1, 0.2, 0.3]`: the left-to-right sum, bit for bit; a NaN element gives the canonical NaN.
103. `T.zero` with `T` unbounded, and `T.missing()`: reject `E0305`.
104. `T: [Additive<T>, Monoid<T>]` both with `zero() -> T`: accept, one requirement. With different signatures: reject `E0577`.
105. `Additive<int>.zero()` and `value.zero()` on `value: Additive<Money>`: reject `E0576`.
106. `Money.zero()`: accept; an ordinary static call.
107. `let f = T.add;`: reject `E0576`; a requirement is called, not taken as a value, in 0.3.0.
108. An implementation whose class has a static initializer: `sum<Money>` runs it at the first `T.zero()`, and a `spawn` of that specialization is rejected as it is for any call that may initialize.
109. `#Pure` on `sum`: decided per specialization; pure for `int32`, rejected (`E0917`) at the specialization whose `add` is not.
110. A generic function with a static bound compiled to `.nir`, instantiated by an importing program with its own class and with `int32`: accept, same image as from source.
111. `class<T> implements Additive<Vec<T>>`: reject `E0501` (a static member using the class's type parameter).
112. `module implements Additive<int> (...)` in user code: reject `E0578`.
113. `T.add(right: b, left: a)` (M5): `b` evaluated first, then `a`.
114. A type parameter shadowed by a local named `T`: the local wins, as lookup already orders them, and `T.zero()` is a member call on the local's value.

Derivation:

115. `let UserJson = derive json.Codec<User>;`: accept. With `{ }`: accept, the same module.
116. `{ field User.name: "display_name"; }`: accept; `encode` emits `{"id":7,"display_name":"Ada","active":true}`.
117. A mapping without `;`, with a name in place of the string, with `"a" + "b"`, or beginning with another word: reject `E0202`.
118. `derive json.Codec<User, Team>` and `derive json.Codec`: reject `E0417` and `E0202`.
119. `derive App.Codec<User>` and `derive json.JsonValue<User>`: reject `E0579`.
120. `let c = derive json.Codec<User>;` inside a function, and `f(derive json.Codec<User>)`: reject `E0579` and `E0202`.
121. `field User.nick: "n";`, and `field Audit.at: "t";` with `Audit` unreachable: reject `E0318`.
122. `field User.name: "a"; field User.name: "b";`, `field User.name: "id";`, and `field User.name: "";`: reject `E0319`.
123. `variant Mode.Fast: "fast";` and `field Shape.Circle.radius: "r";`: accept.
124. `derive json.Codec<Node>` with `Node` holding `Node`: reject `E0590`, with the path.
125. A root that is a class, a tuple, `Optional<User>` or a primitive: reject `E0590`.
126. A field `Optional<int>`: omitted when `None`; missing decodes to `None`; `null` is an error. A field `Optional<Team>` with `Team` a record: `null` decodes to `Some(nil)`.
127. `Optional<Optional<int>>`, a tuple element or an enum payload of `Optional`: reject `E0590`.
128. `int64` and `uint64` at their bounds round-trip exactly; `1.0`, `1e2` and a value out of range for an integer field return `json.codec.range`.
129. A NaN field: `encode` returns `json.codec.not_finite`. `1e999` for a `float64` field: `decode` returns the same.
130. A missing field, a field given twice, a member of the wrong type, an unknown member: errors with the path for the first three; the unknown member is ignored.
131. An enum: `{"variant":"Fast"}`; with `"payload"` for a fieldless variant, a short payload array, a missing or repeated `"variant"`, an unknown variant: errors.
132. A nil record field encodes `null` and decodes to nil; `decode("null")` is `Ok(nil)`.
133. A text of 16,777,217 bytes: `json.codec.too_large` without parsing. Nesting beyond `Json.MaxDepth`: the parser's error.
134. Malformed JSON: the error `Json.parse` returns, unchanged.
135. The codec in an image with `NAME` and `SMAP` removed: same output and same errors.
136. Two derivations for `User` with different mappings: accept; independent.
137. A derivation in file A for a record imported from B, and one imported from a `.nir` module: accept.
138. `let derive = 1; let x = derive + 1;`: accept; a name.
139. `let J = derive` at the end of the buffer in the editor: one `E0202`; the rest of the file keeps its symbols.
140. Renaming `User.name` in the editor with an unpinned derivation: the rename inserts `field User.display: "name";`, and `encode` output is unchanged.

Incomplete input in the editor, and the lookahead:

141. `let P = record (id: int64,` at the end of the buffer: one `E0203`; the recovering parser keeps `P` as a record with the field `id`, so hover and completion on `P` work.
142. `let P = record (id` at the end of the buffer: an expression by the lookahead; one `E0203`, and no record is declared.
143. An interface with `let name: string;` and then `|` at the end of the buffer: one `E0202` (expected `get` or `set`); the interface keeps its other members, and completion offers `get;` and `set;`.
144. `export let.override label: string;` followed by `| get.` at the end of the buffer: one `E0202`; completion offers `override:`. Under an ordinary field it offers `virtual:`.
145. `let small = record < limit;` and `let pair = record > 0 ? record : 0;` with a binding `record`: accept; comparisons.
146. `let P = record implements` at the end of the buffer: one `E0206`; no later declaration is lost.

## Order of work

Each step is one commit on the milestone branch with its targeted tests. The last column is about programs that exist before the step: none of their images changes in any step, and the gate checks it. A program that uses a step's feature did not compile before it.

| Step | Content | Targeted tests | Changes images |
| --- | --- | --- | --- |
| 1 | This note | Documentation checks | no |
| 2 | Value members of payload enums | Converter and stdlib value-member tests; cases 40 | no |
| 3 | Records: parser, formatter, `class.record` and `generated`, eligibility, constructor, value members, `E0570`, `E0571`, language-server support | Parser fixtures with spans, cases 1 to 39, NIR round trip, source/NIR image equality | no |
| 4 | Copy construction, `E0572` | Cases 41 to 58, an event-order trace, the allocation counter | no |
| 5 | Interface properties: requirements, adapters, `property.requirement`, `E0573` | Cases 59 to 77, cross-mode runs of a hot interface read | no |
| 6 | Virtual accessors and overrides: `accessor.virtual`, `E0574`, `E0575` | Cases 78 to 94, layout checks, cross-mode runs | no |
| 7 | Static requirements: `ConformanceCall`, the NeuraIR version, `static.implements`, verifier, specializer, analyses, `E0576`, `E0577` | Cases 95 to 111 and 113 to 114 with user classes, invalid-IR tests, old-reader rejection | no |
| 8 | Primitive conformance: `module implements`, `Std.Core.Arithmetic`, the companions, `E0578` | Cases 100 to 102, 112; every width; float vectors; stdlib image identity | no |
| 9 | JSON derivation: `Codec`, the generator, `--show-derived`, rename pins, `E0579`, `E0318`, `E0319`, `E0590` | Cases 115 to 140, round-trip and rejection vectors, stripped images | no |
| 10 | Language pages, diagnostic pages, examples, changelog, migration notes; benchmarks; the gate | The full gate | no |

## Gate and acceptance

- Every image of the identity set, the extended set and the fuzzer is byte-identical before and after the milestone, since none of those programs uses its features. This includes the standard library changes of steps 8 and 9: a new interface in `json.nra` and new companion functions must not move a byte of an image that does not reach them. If they do, the step is not accepted as it stands.
- On the new binary, every execution mode gives what `--tier=0` gives for the milestone's programs, including steps and peak cells, with tier 3's inlining on and off.
- Each program builds the same image from source and from source through a serialized `.nir`, and repeat builds reproduce it.
- Generated bodies conform: for records, a hand-written class with the same fields, constructor and value members builds the same image apart from names; a copy builds the image of the written-out constructor call; a derived module gives the output, steps and peak cells of its `--show-derived` text compiled in its place.
- Equality, hash and schema are stable: the vectors of cases 30 to 35, and the wire text of case 116 and its family, are the same optimized and unoptimized, in every tier, from `.nir`, and with optional metadata stripped. Hash values are checked against hand-computed constants.
- A copy is one allocation, by the heap counter; replacement order is checked by an event trace; a nil source traps before the first replacement.
- Property dispatch: field adapters, read-only and writable requirements, base and interface dispatch, overrides and each invalid override, layouts with no duplicated field, side-effecting getters, `&` rejection, and the attributes after a NIR round trip and after linking.
- Static requirements: several integer widths, float edge values, a user-defined type, missing and ambiguous requirements, specialization and linking through `.nir`, class initialization, effect propagation, and a check that no specialized program contains a `ConformanceCall` or a dispatch through `any`.
- The codec: exact wide integers, every listed rejection, the bounds, and execution with `NAME` and `SMAP` removed.
- Each rejection in this note has a test with its code, span and message, and each new code has its explain page and its entry in the catalogue.
- Benchmarks record interface-property reads in a hot loop, record equality and hashing, and a codec round trip, against a baseline taken on the same machine; the performance gate of the plan (no program and not the geometric mean more than 5% slower) holds for the existing programs.
- The debugger's raw view of a record and of a class with virtual accessors runs no user code.

## Migration

For the migration guide:

| Old source | Change |
| --- | --- |
| `new with(...)` constructing a class named `with` | Rename the class. None exists in the repository. |
| A call of a function named `record` with a named first argument as the whole initializer of an unannotated declaration | Write `(record)(...)`. No source in the repository is affected; the existing `record()` and `record("B")` calls are unchanged. |
| An immutable data class with hand-written `init`, `equals`, `hash_code` and `to_string` | May become a record when every field is record-eligible. Its text changes to the record form, and its hash to the record formula, so a stored hash or a golden text must be regenerated. Not automatic. |
| Hand-written `to_json` and `from_json` | May become a derivation. Pin every wire name that differs from the field name. The enum shape is the one above; a hand-written shape that differs stays hand-written. |
| An interface with a getter method `name()` | May become a property; callers drop the parentheses. Not automatic. |
| The documentation's statement that accessors are never virtual and that interfaces cannot require them, and that a payload enum has no value members | Rewritten in [Types](../lang/Types.md), [Polymorphism](../lang/Polymorphism.md) and [the standard library](../lang/Stdlib.md). |
| `Std.Core.Int32` and the other companions | Gain `zero`, `add`, `one` and `multiply`, so `x.add(y)` and `x.multiply(y)` on a primitive become companion calls. An extension function of one of those names on a primitive (M5) is shadowed by the companion, as extensions are by every companion member. |

The standard library keeps its classes in this milestone. Converting library types to records is the application-contracts work, which also adds `FrozenList`.

## Decisions taken without the owner

1. **A record is always a heap object**, never inline value words, because identity equality, nil, one allocation per copy and the exclusion of value-struct classes each require it.
2. **Only records are class-typed record fields.** Other `#Immutable` classes are rejected until a derivation design checks their value contracts, since the compiler cannot verify that a hand-written `equals` agrees with its `hash_code`.
3. **Records are not generic, have no methods and no defaults in 0.3.0**, the stricter reading of "data-only"; **they may implement interfaces** their fields and generated members satisfy, so that a record can meet a property contract.
4. **Payload enums gain value members**, since records must hold `Optional` and other payload enums and no contract existed for them.
5. **Record hash is the tuple formula without the type; record text is the qualified name with named fields.** Both are free of identity numbers and image-local identifiers, so they are stable across builds.
6. **A nil source traps immediately after it is evaluated**, before any replacement, so the failure does not depend on which fields are replaced.
7. **`new with(r) { }` is accepted**, as the copy with no replacement.
8. **`record` is recognized by lookahead and `new with` unconditionally**; the two spellings that change meaning are listed under Migration.
9. **Components are not grouped**, as fields and enum payloads are not, which also keeps the lookahead exact.
10. **A property requirement needs `get`**; a write-only requirement and a default-bodied property are rejected.
11. **A property is satisfied by exact type and by outside-readable or outside-writable fields only**, and requirements are hidden bodyless functions named like accessors, so that dispatch needs no new runtime rule and no format version.
12. **`let.override` requires `.override` on its clauses, and the property's name inside an override clause is the inherited accessor called statically**, the only reading under which the canonical example does not recurse and the base's storage stays private.
13. **Static requirements are called through a new NeuraIR operation** under the next free minor version, because a direct call of a bodyless function is invalid in every released reader.
14. **Primitive conformance is declared by `module implements` on trusted companions**, with no new tag; the contracts are `Additive`, `Multiplicative`, `Ordered` and `Parseable`, and floats are not `Ordered`.
15. **A requirement cannot be taken as a function value, and no call goes through the interface's name or a value.**
16. **The generator is identified by the trusted declaration `Std.Encoding.Json.Codec`**, not by a tag, and a derivation is a file or module member only.
17. **The codec's functions return `Result` in both directions** (a non-finite float fails encoding), its root is a record or enum, integers decode only from integer-form numbers, error codes are the eight `json.codec.*` strings, and the input bound is 16 MiB.
18. **A shape that can reach itself is rejected by the codec** although records allow it, since the first generator is acyclic.
19. **Rename pins the old wire name by default.**
20. **`destroy` of a record is rejected.**
