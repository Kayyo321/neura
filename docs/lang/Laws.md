# Laws

A law declares a proposition that the compiler checks with a proof. It quantifies over every value in its domain. A module can export a law as part of its contract or keep it private for use in its own proofs.

In a `let` or `const` initializer, `law()` starts a law declaration and needs a claim. Parenthesize the callee as `(law)()` to call a zero-argument function named `law`.

<!-- example: check-file: examples/laws/boolean.nra -->
```neura
let BooleanLaws = module (
    .Pure
    let invert = func(flag: bool) -> bool => not flag;

    /// Two inversions preserve every Boolean.
    .Since("0.3.1")
    export let DoubleInversion = law (
        for flag: bool;
        claim: { invert(invert(flag)) == flag : bool };
    );
        | proof: {
            match (flag) {
                false => {==},
                true => {==},
            }
        };
);
```

The proof considers both Boolean values. In each case `{==}` reduces the two sides and checks that the resulting terms are equal. The [source example](../../examples/laws/boolean.nra) can be checked with `neura prove examples/laws/boolean.nra`.

## Statements and proofs

`for` introduces a universal variable, `given` introduces a hypothesis, and `last` names a pure expression. Use `claim` for a proposition or `holds` for a Boolean expression that must be true. More than one claim forms a conjunction. An `exists` binding requires a value supplied by a `witness` proof step.

Phase 1 covers Booleans, enums with or without payloads, tuples, and immutable class values. A class quantifier ranges over live instances by default. Write `for?` to include `nil`. Propositional equality compares the contents of values according to their types. Laws can call pure functions whose admissible operations terminate without trapping.

Use `match` to refine an enum or Boolean. Within a constructor arm, a recursive law call can serve as an induction hypothesis if it uses a strictly smaller structural subterm. `rewrite proof in { ... }` requires a motive with a hole written `_`. `rewrite.rev` reverses the direction. Use `have` to name an intermediate proof, `calc` for an equality chain, and `split` for a conjunction. `decide` checks closed or finite goals within the proof fuel budget.

## Module inheritance

An inheritable module can declare an abstract law by omitting its `| proof:` clause. A concrete derived module supplies the proof with `let.proof LawName: { ... };`. The inherited statement stays fixed. An override that changes a function used in an inherited proof causes the compiler to check the proof against the new implementation.

Separately compiled modules retain law statements and certificates in [NeuraIR 1.16](../NirFormat.md#revision-116-laws). A compiler reading an artifact verifies the certificates again. Once verification finishes, the compiler removes laws and proofs before optimization and circuit conversion.

## Diagnostics and later phases

`neura prove` checks laws without compiling a circuit. A failed proof also prevents `neura check` and `neura build` from succeeding. [Diagnostics](../diagnostics/README.md#e14xx-laws-and-proofs) lists the law codes. E1412 is reserved for trap propositions. E1417 and E1418 are reserved for class invariants, and E1419 for the independent verdict checker. E1420 rejects an axiom declaration outside the trusted standard library.

The [design](../design/Laws031.md#7-phased-rollout-plan) describes later phases for machine numbers, traps, and trusted axioms, as well as interface and class declarations, generic laws, project-wide laws and loop induction. A later-phase construct receives a diagnostic when used with a Phase 1 compiler.
