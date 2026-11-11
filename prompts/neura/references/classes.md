# Classes: fields, `last`, accessors, construction, and anonymous classes

How 0.2.0 writes classes. [syntax.md](syntax.md#classes) has the basics (members, `init`, `instance`, inheritance, interfaces); this page covers the features that change how a class is written. Specification: [Types, Accessors](https://github.com/Kayyo321/neura/blob/main/docs/lang/Types.md#accessors), [Anonymous classes](https://github.com/Kayyo321/neura/blob/main/docs/lang/Types.md#anonymous-classes), [Object lifecycle](https://github.com/Kayyo321/neura/blob/main/docs/lang/ObjectLifecycle.md).

## Contents

- [Choosing a field's form](#choosing-a-fields-form)
- [`last` fields](#last-fields)
- [Accessors](#accessors)
- [Target-typed `new`](#target-typed-new)
- [Typed and listed destructuring](#typed-and-listed-destructuring)
- [Anonymous classes](#anonymous-classes)

## Choosing a field's form

| The field | Write | Outside the class |
| --- | --- | --- |
| is set once, by the initializer or by `init` | `export last name: string;` | read `obj.name`; assigning is `E0445`/`E0401` |
| is changed by the class, read by callers | `export let size: int = 0;` + `\| get: size;` | read `obj.size`; assigning is `E0510` |
| is checked or reacted to on every write | `export let level: int = 0;` + `\| get: level;` + `\| set: { ... };` | read and assign; the setter runs |
| is plain data callers may change | `export let retries: int = 3;` | read and assign |
| is internal | `let cache: int[list] = [];` (or `last`) | not visible |

A field is read as `obj.field`, never `obj.field()`: calling a field is `E0423` `expression is not callable`. Keep a method for a value that is computed, takes arguments, or implements an interface member (an accessor cannot satisfy an interface member).

## `last` fields

<!-- example: check -->
```neura
let Account = class (
    export last owner: string;              // assigned exactly once by every init
    export last limit: int;
    export last currency: string = "EUR";   // assigned by its initializer; no init may assign it

    export let init = func(name: string) {
        owner = name;
        limit = 100;
    } or func(name: string, premium: bool) {
        owner = name;
        if (premium) {
            limit = 1000;
        } else {
            limit = 10;
        }
    };
);
```

- Every `init` overload assigns every `last` field that has no initializer, once, on every path that returns (`E0446`); a class with such a field must declare `init`.
- Inside `init`, the field is readable only after it is assigned on every path (`E0441`); a second assignment is `E0440`, one inside a loop `E0442`.
- Nothing else assigns it: no method, closure, subclass, or other class (`E0445`). Anyone may read it.
- A `last` field cannot have a setter (`E0521`).
- A `last` field holding an object fixes the reference, not the object.

## Accessors

<!-- example: check -->
```neura
let failure = import:Std.Core.Failure;

let Thermostat = class (
    // Read-only outside the class: only the class changes it.
    export let readings: int = 0;
        | get: readings;

    // Checked on every write from outside.
    export let target: int = 20;
        | get: target;
        | set: {
            if (value < 5 || value > 30) {
                failure.trap("target out of range");
            }
            target = value;
        };

    export let record = func(celsius: int) {
        readings++;                          // inside the class: the storage
    };
);

let use = func(device: Thermostat) -> int {
    device.target = 22;                      // calls the setter
    device.target += 1;                      // getter, add, then setter
    device.record(21);
    return device.readings + device.target;  // calls the getters
};
```

- Clauses follow the field's `;`, one per line, indented under it: `| get: expression;` and `| set: statement;` or `| set: { ... };`. Each clause ends with `;`. At most one of each, in either order (write `get` first).
- In a setter, `value` is the incoming value. The setter must store it somewhere or warning `W0005` (`setter-never-stores`) is reported.
- **Inside the declaring class** (its methods, `init`, initializers, accessor bodies), a bare field name and `instance.field` are the raw storage. That is why `target = value;` inside the setter stores instead of recursing.
- **Everywhere else** (other classes, functions, subclasses, and `other.field` for another instance even inside the class), `obj.field` calls the getter and `obj.field = v` the setter. There is no call syntax.
- A compound assignment, `++`, or `--` from outside evaluates the receiver once, calls the getter, computes, and calls the setter. A setter that traps ends the run.
- Get-only fields are read-only outside (`E0510`); set-only fields write-only (`E0515`).
- Accessors are allowed only on `export let`/`export last`/`export const` **instance** fields (`E0516`–`E0519`); a `last` or `const` field takes only a getter (`E0521`); a field named `value` cannot have a setter (`E0522`).
- Accessors are not virtual, cannot be overridden or redeclared by a subclass, and cannot satisfy an interface member.
- A field reached through accessors cannot be passed to a `&T` reference parameter (`E0524`); copy it to a local.
- Tags go above the field: `#Deprecated(...)` then `export let x...` then the clauses.

## Target-typed `new`

When the type is already known, `new` leaves out the class:

<!-- example: check -->
```neura
let Point = class (
    export last x: int;
    export last y: int;

    export let init = func() {
        x = 0;
        y = 0;
    } or func(left: int, top: int) {
        x = left;
        y = top;
    };

    export let moved = func(dx: int, dy: int) -> Point => new(x + dx, y + dy);   // result type
);

let Box = class<T> (
    export last item: T;

    export let init = func(value: T) {
        item = value;
    };
);

let distance = func(from: Point, to: Point) -> int => (to.x - from.x) + (to.y - from.y);

let origin: Point = new;                     // annotation: new Point()
let corner: Point = new(3, 4);               // new Point(3, 4)
let label: Box<string> = new("corner");      // type arguments from the annotation
let far = distance(new, new(10, 10));        // parameter types
let path: Point[list] = [new, new(1, 1)];    // element type of an expected collection

let make = func(flag: bool) -> Point {
    let p: Point = flag ? new(1, 1) : new;   // both arms of an expected conditional
    return p;
};
```

- The expected type comes from: a binding or field annotation, an assignment's destination, a function's result type for `return`, a call parameter (among overloads, a contextual argument), the element type of an expected collection, an expected conditional.
- `new;` is `new T()`, `new(args)` is `new T(args)`: same constructor selection and errors.
- No expected type, or an interface, `class`, a singleton, or a type parameter: `E0420`. So `let a = new;` is an error; write `let a: Point = new;` or `let a = new Point();`.
- `new (T)(x)` is `E0435`; write `new T(x)`.

## Typed and listed destructuring

<!-- example: check -->
```neura
let Point = class (
    export let x: int = 0;
);

let [width, height] = 640, 480;                          // one value per name
const [origin, corner]: [Point, Point] = new, new;        // each value gets its element type
let [name, scale]: [string, float64] = "zoom", 2.0;
let [a, b] = [1, 2];                                      // one value: the tuple itself
```

- Two or more values: one per name (`E0436` otherwise), evaluated left to right before any name is bound. An annotation must be a tuple type (`E0437`).
- Destructuring works with `let` and `const`, in blocks and at file level, not as a module or class member. `last` does not destructure (`E0536`): use `const [a, b] = ...;`.

## Anonymous classes

A class body can be written wherever a type is expected:

<!-- example: check -->
```neura
let describe = func(point: class (export let x: int; export let y: int;)) -> string {
    return point.x.to_string() + "," + point.y.to_string();
};

let origin: class (
    export let x: int = 0;
    export let y: int = 0;
) = new;                                       // runs the annotation's field initializers

let text = describe(origin);                   // the same fields: the same class

let settings: class (
    export let retries: int = 3;
        | get: retries;                         // an accessor: a class of its own
    export last name: string = "default";
) = new;
```

- **Fields-only** body (only `export let name: Type` fields, no accessors, tags, `last`, `const`, methods, base, or interfaces): one class everywhere it is written, identified by its fields' order, names, and types. Every field must be exported (`E0532`) and typed (`E0534`).
- **Any other** body is a class of its own at that spot, and accepts every member a declared class accepts.
- `new` runs the field initializers of the body in the annotation of the binding it initializes, or the body after `new` (`new class (export let x: int = 1;)()`). Initializers written elsewhere, such as in a parameter type, never run: `E0530`. A target-typed `new` whose type has no body with initializers at hand: `E0533`.
- An anonymous class is not generic: its field types cannot use an enclosing type parameter (`E0531`).
- Use one for a small group of fields passed between one or two places, or a single settings object; declare a named class for anything with methods or shared across modules.
