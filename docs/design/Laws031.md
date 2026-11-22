# Neura laws and mathematical proofs: design and implementation plan

> **Status:** design for Neura 0.3.1. Phase 1 of [the rollout](#7-phased-rollout-plan) ships in 0.3.1. Phases 2 to 4 are on the roadmap and are not implemented. The six design questions the proposal left open are settled in [Decisions](#8-decisions).
> **Syntax fence note:** every syntax illustration uses a `text` fence, so `doc-examples` does not compile this page. The page holds no compiler implementation code. It specifies the surface grammar, the semantics, the scoping and inheritance model, the proof theory, and the contracts of the compiler pipeline.
> **Registries:** the proposal was written against earlier registries. This page uses the current ones: law records require NeuraIR 1.16, `SymbolKind::Law` takes ordinal 14, and the diagnostics take `E1401` to `E1421`, because `E1100` already belongs to expressions. Tags in the examples use the dot spelling of [the 0.3.1 specification](../lang/Neura0.3.1.md#dot-tags); prose names a tag as `#Name`, as diagnostics print it.

---

## 1. Vision and philosophy: bringing Bend-style laws to Neura

Neura compiles statically typed programs into deterministic neuron circuits and already treats verification as a first-class toolchain responsibility (from checked tags like `#Pure` and `#Immutable`, to behavior-preserving optimization, to `neura_lift`'s structural circuit proofs). **Laws** extend that contract upward into the source language itself.

Inspired directly by **Bend** (`https://bend-lang.org/`), a Neura **law** is **not** a unit test, a runtime assertion, or a property-based fuzzer. It is a **compile-time mathematical theorem** attached to a module, interface, or class that the compiler mechanically verifies before code can build:

1. **Propositions as types, proofs as explicit terms.** A law states a universally quantified logical proposition (most commonly an equational identity, invariant, or impossibility claim). Its `| proof:` supplies a constructive mathematical proof inhabiting that proposition.
2. **Fast, deterministic, tactic-free verification.** Following Bend's core insight, Neura laws reject slow black-box SMT guesswork, fragile heuristic tactic scripts, and heavyweight unification or typeclass search. Proofs are explicit (explicit pattern matching, explicit structural or well-founded induction, explicit rewrite motives), making verification **sub-second**, **deterministic across machines**, and legible when a proof fails.
3. **Immutable behavioral guardrails (human and AI workflow).** Like Bend's `LAWS.bend` / `PROOF.bend` separation, laws act as compiler-enforced guardrails. A base module, interface, or project law file declares what *must always hold*; derived modules, concrete implementations, or downstream edits **cannot weaken, omit, or override** a law statement. Any code change that violates a law fails compilation immediately.
4. **Zero runtime footprint (complete proof erasure).** Every law and proof is verified during compilation and **completely erased** before `IRtoNeuronConversion`. An `.nvr` circuit image contains zero proof neurons, zero proof weights, and zero runtime overhead.
5. **Honest boundaries against Neura's exact semantics.** Unlike a pure lazy functional language, Neura has checked fixed-width integers that trap on overflow, exact IEEE 754 floats (with `-0.0` and canonical quiet `NaN`), mutable classes, `nil` references, host I/O, C FFI, built-in C kernels, and concurrent tasks. Neura's proof system models these semantics faithfully rather than pretending Neura is unbounded pure math.

> **Distinction from tested contract properties:** earlier design studies (such as property-tested contract checks over `Std.Test`) establish *tested* or *exhaustively tested* evidence by running inputs on the NVM. Bend-style laws establish **mechanically proven** universal claims at compile time. The two are complementary, never conflated: a tested property never counts as a proof of a `law`, and diagnostics and reports always keep *proven* strictly distinct from *tested*.

---

## 2. Surface syntax

### 2.1 Anatomy of a law declaration and its attached `| proof:` clause

Neura uses parentheses `( ... )` for structured declarations (`module`, `class`, `interface`, `enum`), braces `{ ... }` for executable or stepwise blocks, a trailing `;` on every declaration, and indented `| clause:` attachments directly after `;` (established by field accessors `| get:` and `| set:`).

Laws follow that exact visual and grammatical form:

```text
let MyModule = module (
    /// Documentation comments and tags sit above the declaration,
    /// never between the law's ';' and its '| proof:' clause.
    .Since("0.3.1")
    export let MyLaw = law (
        // 1. Universal quantifiers (inputs to the theorem)
        for x: SomeType;
        for y: OtherType;

        // 2. Optional preconditions / hypotheses (assumptions with proof witnesses)
        given h_valid: some_predicate(x) == true;

        // 3. Optional local pure abbreviations (definitional shorthand)
        last combined = combine(x, y);

        // 4. Optional explicit termination measure (for non-structural induction)
        measure: x;

        // 5. One or more claims (the proposition to prove)
        claim: { combined == combine(y, x) : ResultType };
    );
        | proof: {
            // Constructive mathematical proof steps discharging the claim(s).
        };
);
```

#### Key structural rules

- **Declaration introducer (`let` or `const`):** both `let MyLaw = law (...);` and `const MyLaw = law (...);` are accepted syntactically, and `let` is the standard style (matching `let MyModule = module (...)`, `let MyClass = class (...)`, `let MyEnum = enum (...)`). Regardless of introducer, a law binding is **always immutable and non-overridable**: a law can never be reassigned, and its statement can never be replaced with `let.override`.
- **Naming convention:** law names use **`PascalCase`** (matching Neura's rule that types, modules, enums, and constants use `PascalCase`, since a law is a named mathematical proposition rather than a runtime function). Quantified variables (`for`), local abbreviations (`last`), and hypothesis witnesses (`given`) use **`snake_case`**.
- **Visibility (`export` or unexported):**
  - `export let MyLaw = law (...);` is part of the enclosing module's, interface's or class's **public behavioral contract**: it is inherited by all derived modules, subclasses and implementations, visible to importers for use inside their own proofs, and included in `neura docs`.
  - Unexported `let HelperLemma = law (...);` is an **internal lemma** private to the declaring scope (usable inside the declaring scope's proofs, including over private helpers, subject to the inheritance soundness rule in section 4.1).
- **Abstract and concrete laws (optional `| proof:`):**
  - Omitting `| proof:` declares an **abstract law obligation** (parallel to a bodyless function `export let f = func(...) -> ...;` in a module interface or `interface`).
  - Abstract laws are permitted in inheritable `let` modules, abstract classes, and interfaces. They are **forbidden** in closed or concrete units (`const` modules, `#Final` concrete classes, or any module or class instantiated or linked as a concrete provider without a matching fulfillment).
- **Attached `| proof:` forms (mirroring `| get:` / `| set:`):**
  - **Single-expression proof** (for direct computational reflexivity or lemma delegation):
    ```text
    export let DoubleZero = law (
        claim: { double(0) == 0 : int32 };
    );
        | proof: {==};
    ```
  - **Block proof** (for multi-step case splits, rewrites, induction, and sub-lemmas):
    ```text
    export let AddZeroRight = law (
        for x: Nat;
        claim: { add(x, Nat.Zero) == x : Nat };
    );
        | proof: {
            match (x) {
                Nat.Zero => {==},
                Nat.Succ(p) => {
                    rewrite AddZeroRight(p) in { Nat.Succ(_) == Nat.Succ(p) : Nat };
                    {==}
                },
            }
        };
    ```
- **Scope inside `| proof:`:** every type parameter `<T>`, every `for` variable, every named `given` hypothesis, and every local `last` abbreviation declared in `law (...)` is **automatically in scope** inside `| proof:`, which removes redundant parameter re-declaration while preserving Bend's exact dependency structure.
- **Generic laws:** just like `func<T>` and `class<T>`, laws accept type parameters and bounds directly after `law`:
  ```text
  export let IdentityLeft = law<T> (
      for x: T;
      claim: { identity<T>(x) == x : T };
  );
      | proof: {==};
  ```
- **Tags and documentation:** `///` doc comments and tags (such as `#Since`, `#Deprecated`, `#Internal`, `#Hidden`, `#Note`) sit immediately above `let MyLaw = law (...);`. No tag may appear between `;` and `| proof:`.

---

### 2.2 What goes inside `law ( ... )`, clause by clause

Every item inside `law ( ... )` ends with `;` (consistent with every declaration in Neura) and falls into six orthogonal clauses, written in logical reading order (quantifiers, preconditions, abbreviations, measure, claims):

| Clause | Syntax shape | Mathematical meaning |
| --- | --- | --- |
| **Universal quantifier** | `for x: Type;` or `for (x: T1, y: T2);` | Universal quantification over `Type`. Introduces an arbitrary well-typed value of `Type` into the law and proof context. For class and interface types, quantifies over **non-nil live instances** by default; `for? x: ClassType;` includes `nil` ([decision 2](#8-decisions)). |
| **Precondition / hypothesis** | `given h: Prop;` or `given h: bool_expr;` (the name is optional, as `given Prop;`, when unused) | Logical implication. Supplies a named proof witness `h` of proposition `Prop` (a boolean expression `cond` desugars to `{ (cond) == true : bool }`). Multiple `given` clauses curry left to right. |
| **Local abbreviation** | `last name = expr;` or `last name: Type = expr;` | Pure definitional binding (`let` mutation is disallowed inside `law (...)`). Unfolds transparently during normalization; exists solely for readability and avoiding repetition. |
| **Termination measure** | `measure: expr;` (optional) | Declares a well-founded termination measure (a non-negative integer expression or tuple of measures) when an inductive proof recurses on something other than a direct structural enum subterm. |
| **Existential quantifier** | `exists y: Type;` (before `claim:`) | Constructive existential (dependent Sigma type). The proof supplies a concrete witness with `witness expr;` and then proves the claim for that witness. A plain class existential requires a live instance. Nullable existential syntax (`exists?`) is deferred. |
| **Claim (goal proposition)** | `claim: Prop;` / `holds: bool_expr;` / `no_trap: expr;` / `traps[(TrapClass)]: expr;` / `outcome: { lhs == rhs : Type };` / `refutes;` | The theorem goal(s) the proof must discharge. Multiple claim clauses in one `law (...)` form a conjunction (a proof discharges them with `split` or named claim blocks). |

Quantifiers and hypotheses nest in source order. A witness can use variables and hypotheses already in scope at its existential. A universal or named hypothesis that follows an existential enters the proof scope through `intro` after the witness.

#### Proposition forms (`Prop`)

A proposition `Prop` (valid in `claim:`, `given`, `have`, and rewrite motives) is built from a small, orthogonal algebra of constructive logical forms:

1. **Propositional equality (Bend's core form):**
   - `{ lhs == rhs : Type }` (explicit type annotation, canonical Bend form).
   - `{ lhs == rhs }` (permitted when `lhs` and `rhs` have unambiguous identical static Neura types without contextual literals; if ambiguous, the compiler requires `: Type`).
   - **Default meaning:** total Leibniz, structural equality. Both `lhs` and `rhs` **must terminate without trapping** and evaluate to indistinguishable canonical values of `Type` (see section 3.1 for exact per-type equality rules, especially floats and classes).
2. **Boolean truth shorthand:**
   - `holds: expr;` is exact syntactic sugar for `claim: { (expr) == true : bool };` (requiring `expr` to be total and trap-free, or proven so under `given`).
3. **Trap and partiality modalities (honest Neura semantics):**
   - `no_trap: expr;` proves `expr` terminates normally without any runtime trap (arithmetic overflow, division by zero, out-of-bounds index, nil dereference, failed cast, `trap_with_message`).
   - `traps: expr;` or `traps(ArithmeticError): expr;` proves `expr` deterministically terminates in a trap (or that specific trap class from `docs/lang/Errors.md`).
   - `outcome: { lhs == rhs : Type };` is **Kleene, observational outcome equality**: either both sides terminate normally with Leibniz-equal values, or both sides trap with the identical trap class.
4. **Impossibility and negation (`Empty`):**
   - `refutes;` (as a claim) or `not Prop` (inside a compound proposition) desugars to `Prop -> Empty`, proving the hypotheses are jointly impossible (for example, Bend's "winning from an illegal board state is impossible").
5. **Logical connectives:**
   - **Conjunction:** `all (P, Q, ...)` or multiple `claim:` entries.
   - **Disjunction:** `either (P, Q)` (constructive sum: the proof must explicitly pick `left` or `right`).
   - **Nested implication and universal:** `for x: T => Prop` and `given Prop => Prop` (for higher-order propositions or inductive predicates).
6. **Law and predicate reference:**
   - Naming an existing law `OtherLaw(args...)` in `Prop` position denotes the proposition that `OtherLaw` claims for those arguments.
   - Predicates (such as `IsEven`, `Sorted`, `InBounds`) are boolean decision functions with checked witnesses in Phase 1; indexed proof-only `prop` types wait for Phase 3 ([decision 4](#8-decisions)).

---

### 2.3 Fulfilling and re-proving inherited or separated laws (`let.proof`)

When a law is declared **without** a `| proof:` (an abstract law in a base module, interface, or `laws.nra`), or when a derived module or subclass **overrides** a function that invalidates an ancestor's proof, the implementing unit supplies a proof using **`let.proof`**, matching Neura's existing `let.<modifier>` family (`let.override`, `let.promote`, `let.as`):

```text
let Codec = module (
    .Pure
    export let encode = func(flag: bool) -> uint8;
    .Pure
    export let decode = func(code: uint8) -> Optional<bool>;

    export let RoundTrip = law (
        for flag: bool;
        claim: { decode(encode(flag)) == Optional<bool>.Some(flag) : Optional<bool> };
    );
);

let BinaryCodec = module:Codec (
    .Pure
    export let.override encode = func(flag: bool) -> uint8 => flag ? 1 : 0;

    .Pure
    export let.override decode = func(code: uint8) -> Optional<bool> {
        if (code == 0) { return Optional<bool>.Some(false); }
        if (code == 1) { return Optional<bool>.Some(true); }
        return Optional<bool>.None;
    };

    let.proof RoundTrip: {
        match (flag) {
            true => {==},
            false => {==},
        }
    };
);
```

#### Rules for `let.proof`

- **Non-overridable law statements:** writing `let.override RoundTrip = law (...);` is a hard compile-time error (`E1405`). A derived module can **never** weaken, replace, or remove an ancestor's law statement; it may only supply or update a proof with `let.proof`, or add *new* laws of its own.
- **Qualification on collision:** when a class implements multiple interfaces that declare laws with the same short name, qualify the target: `let.proof InterfaceA.Symmetry: { ... };`.
- **Same scope rules as `| proof:`:** inside `let.proof TargetLaw: ...;`, all `for` variables, `given` hypotheses, and `last` abbreviations of `TargetLaw` are automatically bound with the implementing module's or class's concrete types substituted.
- **Trusted axioms (`Std` only):** inside the compiler-trusted `Std` tree *only* (paralleling `host(...)` and `kernel(...)`, `E0780`), primitive axioms about built-in machine numbers, bitvectors, and built-in C kernels use `| axiom;` in place of `| proof:`. User code can **never** write `| axiom;` (`E1420`).
- **No silent escape hatches:** a development placeholder `| todo;` (an incomplete proof during drafting) is recognized only to allow partial type-checking under an explicit `neura prove --allow-incomplete` flag, emitting a prominent warning and marking the law `INCOMPLETE`; **`neura build` always rejects any incomplete or unproven law unconditionally.**

---

### 2.4 How proofs are written (the `| proof:` proof language)

Proofs are **goal-directed, constructive, and syntax-directed**. At every line inside `| proof: { ... };`, the proof checker maintains:

- **Context**: bound variables, local `last` definitions, named hypotheses (`given` / `have`), and pattern refinements currently active.
- **Current goal**: the exact proposition that remains to be discharged on this branch.

Every proof step either refines the context or goal, transforms the goal, introduces a local proven fact into the context, or closes the goal.

#### Complete set of proof primitives

1. **Reflexivity and computational closure (`{==}`)**
   - Normalizes both sides of the current equality goal `{ lhs == rhs : T }` under the context (unfolding transparent pure functions, reducing branches and matches, folding defined constants).
   - Succeeds if and only if the two normal forms are alpha-equivalent.
   - Directly parallels Bend's `{==}`.

2. **Explicit rewrite with motive (`rewrite`)**
   - The explicit-motive form, left to right by default ([decision 1](#8-decisions)):
     ```text
     // Given e : { a == b : T }
     // Forward: replaces a with b at the hole '_' in M(_)
     //   Checks: norm(current_goal) is norm(M(a))
     //   New goal becomes: M(b)
     rewrite e in { f(_) == g(y) : R };

     // Reverse: replaces b with a at the hole '_' in M(_)
     //   Checks: norm(current_goal) is norm(M(b))
     //   New goal becomes: M(a)
     rewrite.rev e in { f(_) == g(y) : R };
     ```
   - Reverse rewriting is `rewrite.rev` (or `rewrite sym(e)`) and not a lexer-global `<-` token, so Neura's expression `x<-1` (`x < -1`) is never lexically broken.
   - **Shorthand, deferred:** `rewrite e;` without `in M(_)` would synthesize the motive by replacing every exact syntactic occurrence of `a` in the normalized goal with `_`. Phase 1 requires the explicit motive and rejects the shorthand ([decision 3](#8-decisions)). The core kernel always receives an explicit motive.

3. **Case analysis and structural induction (`match` and `if`)**
   - **`match (expr) { ... }`** splits on an enum (`Nat`, `Optional<T>`, `Tree`, a fieldless enum) or `bool`.
     - Every arm refines `expr` to that constructor inside both the context and the goal, and binds payload variables.
     - It must be exhaustive over all constructors not ruled out by `absurd`.
     - **Structural induction:** calling the enclosing law `MyLaw(subterm, ...)` inside a `match` arm produces a proof witness of the induction hypothesis, valid if and only if the recursive argument is a strictly smaller structural subterm (or strictly decreases the declared `measure:`).
   - **`if (h: cond) { ... } else { ... }`** splits on any total boolean expression `cond`, binding `h: { cond == true : bool }` in the `then` arm and `h: { cond == false : bool }` in the `else` arm.

4. **Intermediate lemmas and equational chains (`have` and `calc`)**
   - **Forward step (`have`):** proves an intermediate proposition and adds it to the context:
     ```text
     have step1: { f(x) == g(x) : int32 } = {
         rewrite HelperLaw(x) in { _ == g(x) : int32 };
         {==}
     };
     ```
   - **Calculational chain (`calc`):** syntactic sugar for readable multi-step equational derivations (desugars purely into chained transitivity and `rewrite` calls):
     ```text
     calc {
         add(a, add(b, c))
             == add(add(a, b), c) by AddAssoc(a, b, c);
             == add(add(b, a), c) by { rewrite AddComm(a, b) in { add(_, c) == add(add(b, a), c) : Nat }; {==} };
             == add(b, add(a, c)) by sym(AddAssoc(b, a, c));
     };
     ```

5. **Contradiction, absurdity and constructor injectivity (`absurd`, `contradiction`, `inject`)**
   - **`absurd witness;`**: when `witness` has type `Empty`, immediately discharges any goal (*ex falso quodlibet*).
   - **`contradiction eq_witness;`**: when `eq_witness` equates two distinct constructors (for example `{ true == false : bool }`, `{ Optional.None == Optional.Some(v) : Optional<T> }`, or two distinct integer or string literals `{ 0 == 1 : int32 }`), elaborates a Bend-style discriminator motive routing one constructor to `Unit` and the other to `Empty`, deriving `Empty` and closing the branch.
   - **`inject eq_witness as (h1, h2, ...);`**: given `{ Ctor(a1, a2) == Ctor(b1, b2) : E }`, extracts payload equalities `h1: { a1 == b1 : T1 }`, `h2: { a2 == b2 : T2 }` through projection motives.

6. **Logical connective steps**
   - **Existentials (`exists y: T`):** `witness expr;` instantiates `y` with `expr` and replaces the goal with the substituted claim.
   - **Conjunctions (multiple `claim:` entries or `all`):** `split { arm1; arm2; }` (or labeled arms matching claim order).
   - **Disjunctions (`either (P, Q)`):** `choose_left { ... }` / `choose_right { ... }` to prove; `match (h) { Left(p) => ..., Right(q) => ... }` to eliminate a disjunction hypothesis.
   - **Negation (`not P` / `refutes;`):** `intro h;` brings `h: P` into scope and sets the goal to `Empty`.
   - **Symmetry, transitivity and congruence helpers (provided in `Std.Proof.Eq`, proven as laws themselves):**
     - `sym(e)` proves `{ b == a : T }` from `{ a == b : T }`.
     - `trans(e1, e2)` proves `{ a == c : T }` from `{ a == b : T }` and `{ b == c : T }`.
     - `cong(f, e)` proves `{ f(a) == f(b) : U }` from `{ a == b : T }`.

7. **Finite-domain reflection (`decide`)**
   - For closed or small-domain finite goals (over `bool`, fieldless enums, `uint8`, `int8`, or bounded ranges with compile-time constants), `decide;` expands a verified exhaustive case tree within the deterministic proof fuel budget.
   - **Not an untrusted oracle:** every case is normalized and checked by the same proof kernel; if any input fails, the compiler prints the **exact concrete counterexample**.

8. **Reasoning over imperative loops (`loop_induct`, Phase 4)**
   - Pure straight-line and branching imperative code (`let` reassignment, `last`, `if`/`else`, `match`) is converted automatically into pure SSA bindings during Proof Core lowering and needs no special proof syntax.
   - For a `#Pure` function containing a `while`, `for` or `do` loop, the proof supplies a structured `loop_induct` block specifying:
     - `invariant(state_vars): Prop;`, what holds before and after every iteration.
     - `variant(state_vars): expr;`, a non-negative decreasing measure proving loop termination.
     - Three explicit sub-proofs checked by the proof kernel:
       1. `entry:` the invariant holds for the initial loop state.
       2. `step:` assuming the invariant and the loop condition, the loop body executes without trapping, preserves the invariant, and strictly decreases `variant`.
       3. `exit:` assuming the invariant and the negated loop condition, the post-loop return value satisfies the law's claim.
   - Alternatively (and often cleaner in Bend style), authors prove a single equivalence law between an imperative loop function and a clean structurally recursive specification function, then prove all algebraic laws once over the recursive specification.

---

### 2.5 End-to-end illustrative examples

#### Example A: inductive data type and structural induction (pure Bend style)

```text
let Peano = module (
    export let Nat = enum (Zero, Succ(Nat));

    .Pure
    export let add = func(a: Nat, b: Nat) -> Nat {
        return match (a) {
            Nat.Zero => b,
            Nat.Succ(p) => Nat.Succ(add(p, b)),
        };
    };

    export let AddZeroLeft = law (
        for x: Nat;
        claim: { add(Nat.Zero, x) == x : Nat };
    );
        | proof: {==};

    export let AddZeroRight = law (
        for x: Nat;
        claim: { add(x, Nat.Zero) == x : Nat };
    );
        | proof: {
            match (x) {
                Nat.Zero => {==},
                Nat.Succ(p) => {
                    rewrite AddZeroRight(p) in { Nat.Succ(_) == Nat.Succ(p) : Nat };
                    {==}
                },
            }
        };
);
```

#### Example B: checked fixed-width arithmetic with honest overflow preconditions (Phase 2)

```text
let SafeMath = module (
    .Pure
    export let midpoint_low = func(a: int32, b: int32) -> int32 {
        return a + ((b - a) / 2);
    };

    /// Proves midpoint_low never overflows when 0 <= a <= b,
    /// and always lies in [a, b].
    export let MidpointBounds = law (
        for (a: int32, b: int32);
        given h_nonneg: a >= 0;
        given h_order: a <= b;

        claim: no_trap(midpoint_low(a, b));
        claim: holds(a <= midpoint_low(a, b));
        claim: holds(midpoint_low(a, b) <= b);
    );
        | proof: {
            have h_diff_ok: no_trap(b - a) = Int32.sub_nonneg_ordered(a, b, h_nonneg, h_order);
            have h_half_le: holds((b - a) / 2 <= b - a) = Int32.div2_le(b - a, h_diff_ok);
            split {
                Int32.add_bounded_no_trap(a, (b - a) / 2, b, h_nonneg, h_order, h_half_le);
                Int32.le_add_nonneg(a, (b - a) / 2, ...);
                Int32.add_le_upper(a, (b - a) / 2, b, ...);
            }
        };
);
```

---

### 2.6 Formal EBNF grammar additions

```text
law_decl        = [ tags ] [ "export" ] ( "let" | "const" ) ident "=" law_expr ";" [ proof_clause ] ;
law_expr        = "law" [ generic_params ] "(" { law_item } ")" ;
law_item        = quantifier_item
                | existential_item
                | given_item
                | local_abbrev_item
                | measure_item
                | claim_item ;

quantifier_item = "for" [ "?" ] ( param | "(" param { "," param } [ "," ] ")" ) ";" ;
existential_item= "exists" ( param | "(" param { "," param } [ "," ] ")" ) ";" ;
given_item      = "given" [ ident ":" ] proposition ";" ;
local_abbrev_item = "last" ident [ ":" type ] "=" expr ";" ;
measure_item    = "measure" ":" expr { "," expr } ";" ;
claim_item      = "claim" ":" proposition ";"
                | "holds" ":" expr ";"
                | "no_trap" ":" expr ";"
                | "traps" [ "(" ident ")" ] ":" expr ";"
                | "outcome" ":" eq_proposition ";"
                | "refutes" ";" ;

proposition     = eq_proposition
                | "all" "(" proposition { "," proposition } [ "," ] ")"
                | "either" "(" proposition "," proposition ")"
                | "not" proposition
                | "no_trap" "(" expr ")"
                | "traps" [ "(" ident ")" ] "(" expr ")"
                | "holds" "(" expr ")"
                | "outcome" "(" eq_proposition ")"
                | expr ;

eq_proposition  = "{" expr "==" expr [ ":" type ] "}" ;

proof_clause    = "|" "proof" ":" ( proof_expr | proof_block ) ";"
                | "|" "axiom" ";" ;   // trusted Std modules only

proof_fulfill   = [ tags ] [ "export" ] "let" "." "proof" qualified_name ":" ( proof_expr | proof_block ) ";" ;

proof_block     = "{" { proof_stmt } [ proof_expr ] "}" ;
proof_stmt      = "rewrite" [ "." "rev" ] proof_expr [ "in" motive ] ";"
                | "have" ident ":" proposition "=" ( proof_expr | proof_block ) ";"
                | "last" ident [ ":" type ] "=" expr ";"
                | "witness" expr ";"
                | "intro" ident ";"
                | "inject" proof_expr "as" "(" ident { "," ident } [ "," ] ")" ";"
                | "absurd" proof_expr ";"
                | "contradiction" proof_expr ";"
                | "calc" "{" calc_step { calc_step } "}" ";"
                | "split" "{" { proof_expr ";" | proof_block ";" } "}" ";"
                | "loop_induct" [ "(" ident ")" ] "{" ... "}" ";"
                | proof_branch ;

proof_expr      = "{==}"
                | "decide"
                | "choose_left" ( proof_expr | proof_block )
                | "choose_right" ( proof_expr | proof_block )
                | proof_branch
                | expr ;   // lemma invocation, hypothesis reference, sym/trans/cong

proof_branch    = "match" "(" expr ")" "{" { match_pattern "=>" ( proof_expr | proof_block ) "," } "}"
                | "if" "(" [ ident ":" ] expr ")" proof_block "else" proof_block ;

motive          = proposition ;  // containing one or more '_' holes of the rewritten type
```

In Phase 1 the `in motive` part of `rewrite` is required ([decision 3](#8-decisions)).

---

## 3. Exact semantic contract for Neura types and effects

A Bend-style proof system is only as sound as its fidelity to the host language's actual execution rules. Below are the exact semantic rules bridging Neura's type system to the Proof Core.

### 3.1 Propositional equality and runtime equality (`==`, `===`, IEEE 754)

A critical soundness trap in mainstream languages is conflating **runtime `==`** with **propositional (Leibniz) equality**:

- In Neura (`docs/lang/Numbers.md`), IEEE 754 `NaN == NaN` is `false` (breaks reflexivity) and `-0.0 == +0.0` is `true` even though `(-0.0).to_bits() != (+0.0).to_bits()` (breaks Leibniz substitution).
- On classes (`docs/lang/Types.md`), runtime `a == b` compares *object identity* (heap handle equality), whereas `a === b` calls user-overridable `a.equals(b)`.
- On payload enums, runtime `==` does not exist at all.

Therefore, **propositional equality `{ a == b : T }` is strictly defined per type** as true Leibniz-substitutable equivalence in the Proof Core:

| Neura type `T` | Meaning of `{ a == b : T }` (propositional equality) | How to express runtime equality instead |
| --- | --- | --- |
| `bool`, fieldless `enum` | Identical constructor or value. | Identical to `holds: a == b;` |
| `int8..int64`, `uint8..uint64` | Identical exact fixed-width integer value (after both sides terminate without trapping). | Identical to `holds: a == b;` (when trap-free) |
| `float16`, `bfloat16`, `float32`, `float64` | **Bitwise identity** (`a.to_bits() == b.to_bits()`), honoring Neura's canonical quiet `NaN` rule (`Numbers.md`). Reflexive, symmetric, transitive, and Leibniz-sound. | Write `holds: a == b;` for IEEE 754 numeric equality (`-0.0 == +0.0` is true, `NaN == NaN` is false). |
| `string` | Identical UTF-8 byte sequence (`byte_length` and every `byte_at(i)` equal). | Identical to `holds: a == b;` |
| Tuple `[T1, T2, ...]` | Component-wise propositional equality. | Not applicable |
| Payload `enum` | Identical variant discriminant and propositionally equal payload fields. | Not applicable (runtime `==` does not exist; propositional equality does) |
| `T[]`, `T[list]` (pure values) | Equal length and element-wise propositional equality at every index `0 <= i < length`. | Value equality `holds: a === b;` |
| `#Immutable` `class` | Same concrete class and field-wise propositional equality of all `last`/`const` fields. | `holds: a == b;` (identity) or `holds: a === b;` (`equals`) |
| Mutable `class` / `interface` | Prohibited as unframed value equality; reasoned about through explicit state snapshots (`before` / `after`) or `holds: a === b;` (value `equals`). | `holds: a == b;` or `holds: a === b;` |

### 3.2 Purity, traps, partiality and termination

Neura's existing `PurityAnalysis` (`src/neura-ir/purity.hpp`) defines `#Pure` as: no global writes, no writes to non-fresh objects, no reads of reassignable or mutable globals, no host calls, no scheduler operations, no object identity (default `hash_code`/`to_string`), and no opaque closure calls. However, `#Pure` still permits **traps** and **non-termination**.

Laws require stricter, layered guarantees:

1. **Admissible expressions inside a law statement:**
   - Every function called in a law statement (outside an encapsulated mutable class transition law) **must satisfy `#Pure`** (verified by `PurityAnalysis`, including through dispatch and prebuilt `.nir` interfaces).
   - Host bindings (`host(...)`), C imports (`import:c(...)` / `Std.DynamicForeign`), scheduler operations (`spawn`, `scope`, `select`), and default object-identity operations are **strictly rejected** in laws (`E1410`).
   - Built-in C **kernels** (`kernel("neura.area.name", version)`) are pure and deterministic: in the Proof Core, a kernel is an abstract pure function symbol (supporting congruence) plus any audited `| axiom;` laws declared in its owning trusted `Std` module.
   - Module, file and static globals may be read in a law **only** if they are compile-time `const` bindings of immutable types (no effectful startup or first-use static initialization).
2. **Explicit trap semantics (`Outcome<T>`):**
   - Every potentially trapping Neura operation (`+`, `-`, `*`, `/`, `%`, `<<`, `>>`, checked `as`, collection indexing, `nil` dereference, `trap_with_message`) lifts in the Proof Core into `Outcome<T> = Ok(T) | Trap(TrapClass)`.
   - A standard equality claim `{ lhs == rhs : T }` requires proving **both** `lhs == Ok(v)` and `rhs == Ok(v)` for the same `v : T`, so it is **impossible** to prove `{ (x + 1) - 1 == x : int32 }` without a precondition ruling out `x == int32.Max`.
   - An explicit `outcome: { lhs == rhs : T }` claim allows matching `Trap(c) == Trap(c)` (useful for proving refactoring equivalence of functions that trap on the exact same inputs).
   - A `no_trap: expr;` claim proves that there exists `v: T` such that `expr == Ok(v)`.
3. **Strict termination and acyclicity (logical soundness):**
   - A non-terminating proof term could inhabit `Empty` and prove `0 == 1`. Therefore:
     - **Law-to-law dependency DAG:** the call graph among laws (which laws invoke which other laws in their `| proof:`) must be acyclic, except for verified self or mutual induction on a shared decreasing measure (`E1414`).
     - **Structural termination (Bend-style):** a recursive call to the enclosing law is accepted when at least one inductive enum parameter strictly shrinks to a direct pattern-matched subterm while earlier parameters in the lexicographic order stay unchanged.
     - **Well-founded measure termination:** when recursing on integers, lengths, or derived expressions, `measure: e;` requires proving `0 <= e_rec < e_cur` (on `int64`/`uint64`) at every recursive call site.
     - **Function termination:** any recursive Neura function unfolded by a proof must also pass termination checking (either structurally recursive, or proven terminating with a `measure:`) before its body can be unfolded inside the Proof Core; non-terminating functions remain opaque uninterpreted symbols (so they can never make the logic inconsistent).

---

## 4. Scoping and inheritance: where laws live

Beyond modules and their derivatives, laws are also interface-wide, class-wide and project-wide, across disciplined scopes with explicit boundaries on what "instance-wide" and "project-wide" mean, so Neura never sacrifices modularity, separate compilation, or soundness.

| Scope | Supported? | Role | Inheritance and enforcement model |
| --- | --- | --- | --- |
| **1. Module-wide** | **Yes (core)** | Algebraic and behavioral laws over module functions, types, and constants. | Inherited automatically by every `module:Base` descendant as long as `Base` is a `let` module (`const` modules cannot be inherited from). |
| **2. Interface-wide** | **Yes (first-class)** | Behavioral contracts (Liskov substitution) for `interface` hierarchies (such as `Comparable`, `Eq`, `Codec`). | Inherited by sub-interfaces (`interface:Base`) and **mandatory** for every concrete `class implements I`. Enables parametric proofs in generic functions `<T: I>`. |
| **3. Class- and instance-wide** | **Yes (two tiers)** | (a) Algebraic method laws on `#Immutable` and value classes; (b) **encapsulated state invariants** on mutable classes (`Account.balance >= 0`). | Inherited by every `class:Base` subclass (unless `#Final`). Stateful invariants require a constructor base case, preservation by every exported method and setter, and encapsulated fields. |
| **4. Anonymous class** | **Yes (narrow)** | Laws inside a one-off anonymous class body. | Automatically classifies the body as a "class of its own" (per `Types.md`), leaving fields-only structural anonymous classes unaffected. |
| **5. Project-wide** | **Yes (structured)** | Cross-module system invariants (such as a `Parser`/`Printer` round trip, or `Auth`/`Ledger` conservation) and mandatory build gates. | Expressed through dedicated **cross-module law modules** (`laws.nra` / `proofs.nra`) enforced by `--laws` and the project manifest, and **not** through ambient global injection into unrelated modules. |
| *Ad-hoc per-variable instance laws* | *No (deferred)* | Constraining one mutable variable independently of its class (refinement types like `{ x: int \| x > 0 }`). | A distinct type-system feature (flow-sensitive refinement subtyping); deliberately kept separate from `law` declarations. |

---

### 4.1 Module-wide laws and derivation mechanics

1. **Inheritable (`let`) and closed (`const`) modules:**
   - In Neura (`docs/lang/Modules.md`), `const M = module (...)` cannot be inherited from. All laws inside a `const` module are **closed-world**: every law must be concrete (have a `| proof:`, no abstract laws), and its proof may freely unfold every pure member of `M`.
   - A `let M = module (...)` can be inherited by `let D = module:M (...)`.
2. **Exact inheritance and re-proof rules (the unfolding footprint mechanism):**
   - Every `export` law declared in `M` is automatically part of `D`'s law set (transitively up the entire single-inheritance chain).
   - **Rule 1 (no weakening):** `D` cannot delete, shadow, or `let.override` any ancestor law.
   - **Rule 2 (abstract fulfillment):** if `M` declared an abstract law `L` (no `| proof:`) and `D` is a concrete module (all required functions have bodies, and `D` is not itself an abstract base or contract), `D` **must** supply `let.proof L: ...;`. (An intermediate abstract module may defer `let.proof L` to its concrete children, or prove `L` once if its own partial implementations suffice.)
   - **Rule 3 (footprint-driven proof invalidation on `let.override`):**
     - When the proof kernel verifies `M`'s proof of `L`, it records **`footprint(L)`**: the exact set of overridable symbols whose *concrete bodies* were unfolded (delta-reduced) during checking.
     - Suppose `D = module:M (...)` overrides function `f` with `let.override f = ...;`:
       - **If `f` is not in `footprint(L)`** (`M`'s proof of `L` did not depend on `f`'s body, or depended only on *other laws* of `f`): **`M`'s proof remains mathematically valid in `D` automatically**, with no re-proof.
       - **If `f` is in `footprint(L)`**: `M`'s proof is **invalidated** in `D`.
         1. If `D` provides `let.proof L: ...;`, the compiler checks `D`'s new proof against `D`'s members.
         2. If `D` does *not* provide `let.proof L`, the compiler **re-checks `M`'s original proof** against `D`'s overridden `f` (for example, when `D.f` is a simple equivalent refactoring whose normal form still closes `{==}`).
         3. If re-checking fails, compilation fails with diagnostic `E1406` naming `M.L`, `D`'s override of `f`, and the exact normalized proof mismatch, and `D` must supply `let.proof L` with the inherited statement intact ([decision 5](#8-decisions)).
   - **Rule 4 (soundness of unexported internal lemmas):**
     - Suppose `M` has an *unexported* lemma `PrivateLemma` that unfolds an *overridable exported* function `f`, and an exported law `PublicLaw` uses `PrivateLemma` in its proof.
     - Because footprints propagate transitively through lemma calls, `f` enters `footprint(PublicLaw)`. Therefore, overriding `f` in `D` properly invalidates `PublicLaw` too, making it **impossible** to smuggle a broken assumption through a private lemma.
     - Moreover, if `PrivateLemma` itself is only used inside `M` (not by any exported law), `PrivateLemma` is checked against `M`'s concrete definitions only, unless `PrivateLemma` is called from an inherited function's proof context.

---

### 4.2 Interface-wide laws (checked Liskov substitution, Phase 3)

Interfaces in Neura (`docs/lang/Polymorphism.md`) currently specify only method signatures (and optional default bodies). Interface laws turn them into true **algebraic and behavioral contracts**:

```text
let Comparable = interface<T> (
    .Pure
    let compare_to = func(other: T) -> int32;

    export let Reflexive = law (
        for a: T;
        claim: { a.compare_to(a) == 0 : int32 };
    );

    export let Antisymmetric = law (
        for (a: T, b: T);
        given h_le: a.compare_to(b) <= 0;
        given h_ge: b.compare_to(a) <= 0;
        claim: holds(a.compare_to(b) == 0);
    );

    export let Transitive = law (
        for (a: T, b: T, c: T);
        given h_ab: a.compare_to(b) <= 0;
        given h_bc: b.compare_to(c) <= 0;
        claim: holds(a.compare_to(c) <= 0);
    );
);
```

- **Receiver quantification (`instance` / `Self`):** inside an interface law, either the type parameter `T` (the pattern `Comparable<T>` already uses) or the contextual receiver type **`Self`** (denoting the concrete implementing class) quantifies over non-nil instances of any implementing class.
- **Default method proofs:** an interface with default methods may attach `| proof:` to a law if the proof depends only on default bodies and other interface laws. Any implementing class that overrides a method in the proof's footprint must re-prove it with `let.proof`.
- **Generic parametric power:** inside a generic function `let binary_search = func<T: Comparable<T>>(...)`, a law about `binary_search` can directly invoke `T.Reflexive`, `T.Antisymmetric`, and `T.Transitive` as proven hypotheses. The proof checks **once parametrically** on the generic declaration, and is not re-run for every specialization, because Neura enforces at every `class X implements Comparable<X>` that `X` proved those laws.
- **Multiple interface inheritance:** a class implementing `A` and `B` inherits the union of both interfaces' law obligations; identical law names from distinct interfaces are disambiguated as `let.proof A.LawName:` and `let.proof B.LawName:`.
- **Sealed interfaces (`#Sealed`):** because every implementor of a `#Sealed` interface is known in its declaring module, laws in that module may additionally perform exhaustive case analysis over all sealed implementations.

---

### 4.3 Class- and instance-wide laws (two-tier design)

Classes in Neura come in two fundamentally different flavors, `#Immutable` value classes and mutable stateful classes, so class laws have two clean tiers:

#### Tier 1: pure value and method laws (especially on `#Immutable` classes)

- Any `#Immutable` class (`docs/lang/Types.md`: all fields `last`/`const` of immutable types) maps directly to an algebraic record in the Proof Core.
- Laws quantify over non-nil instances (`for a: Money, b: Money;`) and state algebraic laws over pure methods (`a.plus(b) == b.plus(a)`), constructor laws, and `equals`/`hash_code` consistency (`given h: a.equals(b); claim: { a.hash_code() == b.hash_code() : uint32 };`, mechanically enforcing the rule currently written only as prose in `docs/lang/Types.md`).
- Subclasses (`class:Base`) inherit all class laws under the exact same footprint and re-proof rules as modules; `#Final` classes are closed-world.

#### Tier 2: encapsulated class state invariants (`law.invariant` and state-transition laws, Phase 3)

- For mutable classes (such as `Account` with `balance: int64` and `deposit`/`withdraw`), a class can declare an **instance invariant**:

```text
.Final
let Account = class (
    export last owner: string;

    export let balance: int64 = 0;
        | get: balance;

    export let init = func(name: string) {
        owner = name;
    };

    export let deposit = func(amount: int64) -> bool { ... };
    export let withdraw = func(amount: int64) -> bool { ... };

    export let NonNegativeBalance = law.invariant (
        claim: holds(balance >= 0);
    );
        | proof: {
            // The compiler generates one obligation branch per constructor,
            // per exported method, and per setter that touches guarded fields.
            on init => {==},
            on deposit(amount) => { ... },
            on withdraw(amount) => { ... },
        };
);
```

- **Strict encapsulation precondition (mandatory for soundness):**
  - Every field mentioned in a `law.invariant` **must be encapsulated**: either unexported, `last`/`const` of an immutable type, or `export let` with **no unguarded public write** (get-only `| get:`, or a guarded `| set:` whose setter body is also proven to preserve the invariant).
  - If a field is a plain mutable `export let` (allowing arbitrary external mutation `acc.balance = -50`), the compiler rejects the invariant with diagnostic `E1417` pointing to the exposed field and suggesting making it unexported or adding a get-only or checked setter accessor.
  - No exported method may hand out a mutable primitive reference (`&int64`) aliasing an invariant-governed field (`E1418`).
- **Proof obligations generated automatically for `law.invariant`:**
  1. **Construction (base case):** every `init` constructor overload must establish the invariant upon normal return (after running field initializers and any base constructor).
  2. **Preservation (inductive step):** assuming the invariant holds on entry, every exported method (and every `| set:` accessor) must preserve it on every normal return path (and before any external callback or re-entrant call on the instance).
  3. **Subclass inheritance:** a subclass (`class:Base`) inherits all ancestor invariants; any new constructor or overridden or added method must preserve both base and derived invariants.
  4. **Concurrency synergy with monitors (`#Synchronized`):** because a Neura monitor serializes every exported method and accessor under its per-instance re-entrant guard (`docs/lang/Concurrency.md`), an encapsulated `law.invariant` on a `#Synchronized` monitor is automatically preserved across concurrent tasks at every guard boundary, without needing a full concurrent separation logic in the first version.
- **Per-individual-instance laws:**
  - An **anonymous class** with a `law` is already a "class of its own" (`Types.md`), so one-off instance objects can naturally declare laws without affecting structural fields-only anonymous classes.
  - Arbitrary laws attached to individual mutable variables of an *existing* class (without changing its type) are **refinement types**, not class laws; keeping them separate prevents invasive flow-sensitive subtyping complexity from leaking into the core language.

---

### 4.4 Project-wide laws (Bend's `LAWS.bend` model, done cleanly for Neura; Phase 3)

Laws are **project-wide** through explicit cross-module law modules plus a mandatory compiler build gate, and never through ambient global injection.

#### Why not ambient global laws?

Injecting global laws implicitly into every module would break Neura's explicit-import model (`Modules.md`), create action at a distance when two unrelated modules share names, and break separate compilation of prebuilt `.nir` libraries.

#### The project-wide architecture

1. **Cross-module law modules (`laws.nra`):**
   - A project creates a dedicated law module (for example `ProjectLaws = module (...)` in `laws.nra`) that explicitly imports any combination of project modules (`Parser`, `Printer`, `Ledger`, `Auth`) and declares laws spanning **across** them:
     - Round-trip laws between two modules (`Parser.parse(Printer.print(x)) == Ok(x)`).
     - System conservation invariants (`total_supply(Ledger.transfer(state, a, b, amt)) == total_supply(state)`).
     - Unreachability and safety invariants (`refutes` reaching an unauthorized state from a guest role).
2. **Immutable specification, editable proof (`laws.nra` and `proofs.nra`):**
   - Directly mirroring Bend's `LAWS.bend` and `PROOF.bend` workflow:
     - `laws.nra` declares `export let SystemLaws = module ( export let Conservation = law (...); ... );` with **abstract** laws (human-reviewed, protected by code owners).
     - `proofs.nra` declares `let SystemProofs = module:SystemLaws ( let.proof Conservation: { ... }; ... );` (editable by developers or AI coding agents).
   - Because `SystemProofs` derives from `SystemLaws`, the compiler mechanically guarantees `proofs.nra` proved the **exact** laws in `laws.nra` without altering a single claim.
3. **Mandatory build gate (`--laws` and the project manifest):**
   - Normally, compiling `main.nra` only visits modules reachable from `main.nra`'s imports, so an unimported `laws.nra` would not block a build.
   - `--laws <path.nra>...` on `neura check`, `neura build`, `neura test`, and `neura prove` forces the compiler to load, link, and verify all project law units alongside the program before emitting any `.nvr` artifact, while **still erasing them completely** from the emitted circuit.
   - In a project, the law units are listed in the existing `neura.toml`, and project builds and `neura prove` enforce them automatically, with no flag ([decision 6](#8-decisions)).
4. **Universal family obligations stay structural:**
   - When a project wants *every* module or class in a family (every codec, every game rulebook, every service) to obey a project-wide law, that family derives from a shared base `module` or implements a shared `interface` carrying that law.

---

## 5. Under the hood: compiler pipeline architecture

Neura's compiler strictly separates stages:

```text
C import -> Lexer -> Parser -> NeuraIR (+ Proof Kernel & Erasure) -> IR optimization -> IRtoNeuronConversion -> output (.nvr)
```

Laws respect every stage boundary: **source syntax stays in the Lexer and Parser, semantics and verification live in NeuraIR and the dedicated proof subsystem, proofs are erased before IR optimization, and the neuron backend and the NVM never see a proof.**

| Stage | Ownership for laws | Hands on |
| --- | --- | --- |
| **C import** | None. C-imported modules produce `Std.DynamicForeign` calls, which are impure and forbidden inside laws. | Unchanged ordinary Neura source |
| **Lexer** | None (zero new hard keywords). All law and proof words (`law`, `proof`, `given`, `claim`, `rewrite`, `have`, `absurd`, and the rest) stay ordinary identifiers outside their grammar slots. No global `<-` token (preserves `x<-1`). | Existing token stream with exact spans |
| **Parser** | Parses `law (...)`, attached `| proof:`, `| axiom;`, `let.proof`, `law.invariant`, and proof-block statements and expressions with full spans and multi-error syntax recovery. Does no name resolution, typing, or proving. | Law and proof syntax nodes in the AST |
| **NeuraIR lowering and linking** | Resolves law symbols, inheritance chains, interface obligations, and `let.proof` matchings; checks purity, encapsulation, non-overridability, and type well-formedness; checks generic bounds and `.nir` imports. | Typed NeuraIR and law tables (`.nir` 1.16) |
| **Proof Core and proof kernel** | Translates pure NeuraIR functions and types into the minimal functional Proof Core; verifies termination, acyclicity, and every proof step; records unfolding footprints; optionally cross-verifies with the independent `--verdict` micro-checker. | Verified verdicts and diagnostics (or build failure) |
| **Proof erasure** | Strips all law and proof bodies from executable IR before optimization (keeping only verified facts if and when Phase 4 verified optimization opts in). | Ordinary validated NeuraIR |
| **IR optimization** | Unchanged in 0.3.1: optimizes erased executable IR while preserving observable behavior (so proven laws stay true of optimized IR). Later: may safely consume proven `#Total`/`#NoTrap` facts under existing resource, trap and task contracts. | Validated optimized NeuraIR |
| **IRtoNeuronConversion and output** | Sees zero laws and proofs. Emits byte-for-byte identical neuron graphs, weights, and `.nvr` execution tables. (Optional non-executable `LAWS` attestation metadata section, strippable with zero behavioral impact.) | Standalone `.nvr` image |
| **NVM and decompiler** | Runtime unchanged. Together with `neura_lift`'s structural circuit proofs, forms a verified chain from source laws to neuron circuits under the assumptions in section 5.4.2. | Deterministic execution and inspection |

---

### 5.1 Lexer and parser details (`src/lexer/`, `src/parser/`)

- **Zero breaking lexical changes:**
  - Every new word (`law`, `proof`, `given`, `claim`, `holds`, `no_trap`, `traps`, `outcome`, `refutes`, `exists`, `measure`, `rewrite`, `have`, `witness`, `intro`, `inject`, `absurd`, `contradiction`, `calc`, `split`, `decide`, `loop_induct`, `axiom`, `invariant`, `choose_left`, `choose_right`) is **contextual**, identical to Neura's existing `last`, `scope`, `spawn`, `select`, `get`, `set`, and `value`.
  - `{==}` is parsed from the three existing tokens `{`, `==`, `}` in proof-expression position; no new lexer token is needed.
  - Reverse rewriting uses `rewrite.rev` (reusing `.` and a contextual identifier, matching `let.override` / `let.proof`) rather than adding a `<-` lexer token that would break `x<-1`.
- **Reusing Neura's `|` attached-clause parser path:**
  - Right after parsing a declaration's trailing `;`, the parser already peeks for `|` to parse `| get:` / `| set:` accessor clauses on class fields.
  - Extending that exact peek to recognize `| proof:` and `| axiom;` after a `law` declaration keeps the parser simple and gives crisp diagnostics when someone attaches `| proof:` to a non-law or `| get:` to a law.
- **Disambiguating `law (...)` from calling a function named `law`:**
  - In `let x = law (...)`, `law` starts a law expression if and only if it is followed by a generic-parameter list and `(`, or by `(` whose first token begins a law item (`for`, `exists`, `given`, `last`, `measure`, `claim`, `holds`, `no_trap`, `traps`, `outcome`, `refutes`, or `)`), or by `.invariant (`. An ordinary call such as `let r = law(42);` and a comparison such as `law < limit` retain their expression meaning.
- **Multi-error syntax recovery:**
  - Inside `law (...)` and `| proof: { ... };`, syntax errors recover to the next `;` or matching closing delimiter, respecting `--max-errors` before lowering begins.

---

### 5.2 NeuraIR representation and separate compilation (`.nir` format)

Following `docs/NirFormat.md`'s versioning discipline:

1. **Backward-compatible `.nir` revision 1.16:**
   - Programs containing no laws are written in the earliest version that can encode them, with **unchanged bytes**.
   - Programs with laws are written as `.nir` 1.16, adding:
     - **`SymbolKind::Law`** (the next free ordinal, `14`): owned by `Module`, `Class`, `Interface`, or `File`, carrying `exported`, `generics`, `parameters` (the `for` and `given` bindings), `span`, and the standard symbol `attributes` (for tags, the `law.invariant` flag, and the like).
     - **Law record table** (parallel to `Bodies` and `StaticConstants`, never mixed into executable `Bodies`):
       - `law_symbol: u32`
       - `status: u8` (`0 = Abstract`, `1 = Proven`, `2 = TrustedAxiom`)
       - `statement: CoreTerm` (the typed Proof Core proposition, referencing NeuraIR symbols and types so the linker remaps them identically to ordinary nodes)
       - `footprint: vector<u32>` (the exact set of symbol IDs whose concrete bodies were unfolded by the proof)
       - `certificate: optional<CoreProofTerm>` (a compact serialized Proof Core certificate when `status == Proven`, plus an optional default proof template for inheritable bases).
   - An earlier reader rejects a 1.16 file, and a 1.16 reader rejects a law symbol or law record in a file that declares an earlier version, as the other revisions do for their features.
2. **Linker and prebuilt `.nir` security model:**
   - When `App` imports a prebuilt `lib.nir`:
     - All symbol IDs, type references, and span file indices inside law records are remapped by the linker just like `Bodies` and `Attributes`.
     - **Never trust a `.nir` proof claim:** when `App`'s proofs rely on an imported `Lib.MyLaw`, or when `neura build` links `lib.nir`, the proof kernel **re-verifies `certificate` directly in the Proof Core** (or verifies a cached content hash within the same compiler session). A tampered `.nir` file cannot forge a proven theorem.
     - **Abstract obligations survive `.nir` boundaries:** if `lib.nir` exports an abstract law on `Lib.BaseModule` or `Lib.SomeInterface`, `App` inherits that law statement directly from `lib.nir` and must fulfill it with `let.proof` in `App`.
     - **Override invalidation survives `.nir` boundaries:** because `lib.nir` carries `footprint`, if `App` derives `module:Lib.BaseModule` and overrides a function in `footprint(L)`, the compiler immediately detects the invalidation and applies rule 3 of section 4.1.

---

### 5.3 Inside the Proof Core and the proof kernel (the trusted computing base)

To keep verification **sound, minimal, and sub-second**, proof checking does **not** operate on raw imperative NeuraIR. Instead, NeuraIR lowers pure referenced definitions into a tiny, self-contained **Proof Core** checked by a minimal **proof kernel**.

#### 5.3.1 Architecture of the Proof Core

The Proof Core is an explicit, dependently typed core calculus with **De Bruijn indices** (eliminating variable-capture bugs) and about 14 term forms:

| Core construct | Purpose |
| --- | --- |
| `Var(index)` / `Sym(symbol_id)` | Bound variables (De Bruijn) and references to Neura functions, constants and laws |
| `Lam(type, body)` / `App(fn, arg)` | Pure lambda abstraction and application |
| `Pi(type, codomain)` / `Sigma(type, codomain)` | Dependent universal (implication) and existential (conjunction) propositions |
| `Let(type, val, body)` | Local pure SSA binding (preserves sharing during reduction) |
| `Ctor(enum_or_record, tag, fields)` | Constructors for enums, `bool`, tuples, `#Immutable` class records, `Outcome.Ok` / `Outcome.Trap` |
| `Match(scrutinee, motive, arms)` | Dependent pattern elimination over enums, `bool`, and `Outcome` |
| `Lit(typed_value)` | Exact typed literals (`int8..uint64`, exact float bit patterns, UTF-8 byte strings) |
| `PrimOp(op, args)` | Built-in exact arithmetic (returning `Outcome`), bitvector operations, IEEE float operations, string and sequence operations |
| `Eq(type, lhs, rhs)` | Propositional Leibniz equality type `{ lhs == rhs : T }` |
| `Refl(type, term)` | Reflexivity witness `{==}` inhabiting `Eq(T, t, t)` |
| `J / Transport(eq, motive, proof)` | Leibniz substitution primitive (the target of `rewrite`, `sym`, `trans`, `cong`, `contradiction`, `inject`) |
| `Empty` / `Absurd(motive, witness)` | The uninhabited proposition and *ex falso* elimination |
| `StateStep(class, method, state, args)` | Pure receiver-state transition encoding for encapsulated `law.invariant` methods |

#### 5.3.2 Lowering NeuraIR to the Proof Core

1. **Imperative straight-line and branching code to pure SSA:**
   - Local `let` reassignments, deferred `last` bindings, `if`/`else`, `match`, `?:`, and comparison chains inside a `#Pure` function are converted into pure SSA `Let` and `Match` expressions through standard join-point translation.
   - Example: a mutable accumulator updated in `if`/`else` becomes a pure conditional expression returning the updated tuple of live locals.
2. **Checked arithmetic and failure lifting:**
   - Every potentially trapping NeuraIR operation (binary `+`, `/`, convert, index, class cast, field on a nullable) lowers to an `Outcome<T>`-returning `PrimOp`, chained through `Match` on `Outcome.Ok(v)` / `Outcome.Trap(reason)` in exact Neura left-to-right evaluation order.
   - Expressions statically known trap-free by `PurityAnalysis::cannot_trap` (literals, local loads, `!`, integer comparisons, non-shift bitwise operations) bypass `Outcome` wrapping directly to keep terms small and fast.
3. **Closed and open unfolding tables (footprint tracking):**
   - Each symbol `S` in the Proof Core carries a transparency flag in the current verification context:
     - **Transparent:** concrete `const` module members, `#Final` functions, private helpers, or members of the concrete module or class currently being checked. Unfolding `S` records `S` into the active proof's **`footprint`**.
     - **Opaque:** bodyless requirements, external or abstract generic type-parameter methods, `host` and C calls (rejected), non-terminating functions, or kernel bindings (unfolded only through explicit `Std` axioms).
4. **Generic parametricity:**
   - Checking a generic `law<T: Bound>` leaves `T` as an abstract opaque type variable and exposes only `Bound`'s exported signatures and `Bound`'s proven laws, proving the law simultaneously for **all** valid concrete types `T` without specialization.
   - When a concrete class or module specializes an inherited generic interface law (such as `Rank implements Comparable<Rank>`), `T` is substituted with `Rank` before checking `Rank`'s `let.proof`.

#### 5.3.3 Fast deterministic normalization (the reducer)

To achieve sub-second proof checking without compromising Neura's determinism contract:

1. **Normalization by evaluation with a hash-consed environment machine:**
   - Avoids naive capture-avoiding AST substitution (which causes quadratic or exponential blowups).
   - Evaluates closed and open terms (terms containing free quantified variables) into **weak-head normal form** on demand during equality checks, only descending structurally when heads match, and lazily unfolding transparent function bodies (delta reduction) only when heads differ.
   - **Structural DAG sharing (hash-consing):** equal subterms share identity, so checking `{ t == t : T }` on huge terms finishes in constant time.
2. **Deterministic fuel budget (never wall-clock time):**
   - Every normalization step decrements a deterministic integer counter (`--proof-fuel`, by default `1_000_000` steps per law).
   - Exhausting fuel fails deterministically on every machine with diagnostic `E1415` ("proof normalization fuel exhausted"), naming the hottest unfolded symbol so the user knows whether a recursive definition did not reduce or needs an intermediate `rewrite` or a higher `--proof-fuel`.
3. **Strict bidirectional checking:**
   - The checker **never searches** for proofs, never backtracks, and never guesses higher-order motives.
   - Checking each step is linear in the size of the proof term plus bounded normalization work.

#### 5.3.4 Termination and totality checker

Before any recursive law proof or any recursive function unfolded by a proof is trusted, the termination checker validates:

1. **Call-graph SCC decomposition:** computes strongly connected components of functions and laws.
2. **Non-recursive definitions:** immediately accepted as terminating.
3. **Structural lexicographic order (default, Bend-style):**
   - Tracks which pattern-matched variables are strictly smaller subterms of which parameters (`p < x` inside `Nat.Succ(p)`, or `left < tree` and `right < tree` for `Tree.Branch { left, right }`).
   - Searches for a permutation (lexicographic tuple) of inductive parameters such that every self or mutual recursive call strictly decreases at some position while keeping all earlier positions unchanged (or decreasing).
4. **Well-founded measure order (`measure: expr;`, Phase 2):**
   - When structural recursion does not apply (integer countdown `n - 1`, binary search `hi - lo`, list length decrease), the checker emits an extra sub-goal at every recursive call site requiring proof that `0 <= measure(rec_args) < measure(cur_args)`.
   - If the user does not discharge that obligation, compilation fails with `E1413` showing the exact recursive call site.
5. **Strict positivity for inductive proof types:** any recursive payload enum used in proofs must be strictly positive (already guaranteed in Neura when closure and function-value fields are not allowed inside inductive proof types).

#### 5.3.5 Optional independent kernel cross-check (`--verdict`, Phase 3)

Mirroring Bend's `bend PROOF.bend --verdict`:

- The compiler's **elaborator** (which handles Neura syntax sugar, `calc`, `holds:`, `contradiction`, `loop_induct`, and rich diagnostics) emits a pure **Proof Core certificate** containing only the minimal core primitives.
- Running `neura prove --verdict` (or `neura build --verdict`) passes every emitted certificate to a tiny, standalone, read-only **micro-kernel verifier** (no parser, no optimizer, no elaborator, no sugar) that independently type-checks every certificate from scratch.
- Any bug in the elaborator or syntactic sugar surfaces as a kernel rejection (`E1419`) rather than a false proof.

---

### 5.4 Interaction with optimization, circuit conversion, runtime and decompiler

#### 5.4.1 IR optimization (`src/ir-optimization/`)

- **In 0.3.1 (immediate and safe):**
  - Proof erasure runs **before** IR optimization. The optimizer receives ordinary validated NeuraIR identical to a program without laws.
  - Because every optimization pass in `docs/IrOptimization.md` preserves observable values, traps, effects, and termination, **every law proven on lowered NeuraIR holds identically on optimized NeuraIR**.
- **Phase 4 (verified optimization, never unchecked):**
  - Neura's tag catalogue (`docs/lang/Tags.md`) already reserves `#Total`, `#NoTrap`, `#Idempotent`, `#Commutative`, `#Associative`, `#Requires`, `#Ensures`, and `#Invariant`, refusing to enable them until the compiler can prove them.
  - Laws provide the proof engine to unlock those tags:
    - A function with a proven `no_trap` and termination law legitimately earns `#NoTrap` / `#Total`, allowing `PurityAnalysis::total()` to lift its current narrow syntactic restriction (no loops, no arithmetic) to *proven-total* functions.
    - Proven `#Commutative` / `#Associative` / `#Idempotent` tags can be backed directly by requiring (or desugaring to) their corresponding algebraic `law` and `| proof:`.
  - **Strict guardrails for any law-driven optimization:**
    - Never remove or move a trap unless proven unreachable.
    - Never violate `IrOptimization.md`'s resource monotonicity contract (never increase steps, peak heap cells, or stack frames).
    - Never drop or reorder task polls or scheduler calls in concurrent programs.

#### 5.4.2 IRtoNeuronConversion, `.nvr` output, NVM and decompiler

- **Zero runtime overhead:** `IRtoNeuronConversion` never sees any law or proof. Emitted neuron graphs, weights, biases, exit tables, and execution steps are **byte-for-byte identical** with or without laws.
- **Optional non-executable audit section (`LAWS` in `.nvr`):**
  - Optionally (strippable with `--no-law-metadata`, like `--no-source-map` and `--no-lift-hints`), the output stage may record a non-executable `LAWS` metadata table listing the names, target symbols, and SHA-256 digests of the laws proven at build time, inspectable with `neura inspect`.
  - Removing `LAWS` changes no execution behavior, and the NVM loader ignores it completely.
- **The verification chain (honest claim boundaries):**
  - **Layer 1 (source to NeuraIR):** laws mechanically prove source-level mathematical theorems against NeuraIR semantics.
  - **Layer 2 (NeuraIR to optimized NeuraIR):** behavior-preserving passes (`IrOptimization.md`).
  - **Layer 3 (optimized NeuraIR to neuron circuits):** `neura_lift` (`src/lift/proof.cpp`) structurally proves each block's neuron graph matches its typed IR operations.
  - **Explicit assumptions and limits (never overclaimed):** end-to-end equivalence holds under: (a) soundness of the NeuraIR to Proof Core lowering and of the proof kernel, (b) soundness of any trusted `Std` axioms used, (c) behavior preservation of the optimization passes, (d) correctness of `neura_lift` and circuit conversion, and (e) **finite NVM resource bounds** (`--max-steps`, `--max-frames`, `--max-heap-cells`): a mathematically total function can still halt with `CpuTimeExceeded` or `OutOfMemory` on inputs exceeding runtime resource limits.

---

## 6. Diagnostics and tooling

### 6.1 Dedicated diagnostic codes (`E14xx`, laws and proofs)

`E14xx` is free in `src/util/diagnostic_codes.def`. The proposal's `E11xx` numbers are not used because `E1100` belongs to expressions; each code below is the proposal's code plus 300.

| Code | Condition | What the diagnostic shows |
| --- | --- | --- |
| `E1401` | Reflexivity mismatch (`{==}` failed) | Expected goal, **normalized left side**, **normalized right side**, the exact **first diverging subterm**, and active pattern refinements in scope. |
| `E1402` | Rewrite motive mismatch | Evidence equation `{ a == b : T }`, instantiated motive `M(a)`, and exact difference from the current goal. |
| `E1403` | Unproven or incomplete goal at the end of `| proof:` | Remaining undischarged goal proposition(s) and active context hypotheses (acting as a goal viewer during proof authoring). |
| `E1404` | Missing proof for an inherited abstract law | Points at `module:Base` / `implements I` / `class:Base`, names the unfulfilled law with a secondary label at its declaration, and offers a placeholder `let.proof` stub for `neura check --fix`. |
| `E1405` | Attempted `let.override` on a law | Explains laws cannot be overridden or weakened; suggests `let.proof` to supply a new proof. |
| `E1406` | Override invalidated an ancestor's proof | Labels the `let.override` method or function, names the broken ancestor law, explains that the ancestor's proof unfolded the overridden body, and shows the failed re-check difference. |
| `E1407` | Non-exhaustive `match` in a proof | Lists the uncovered enum variants or cases. |
| `E1408` | Invalid `absurd` / `contradiction` / `inject` | Shows the actual type of the witness (not `Empty`, or not a constructor mismatch). |
| `E1409` | Type mismatch in a law or proof term | Expected and actual type or proposition with exact spans. |
| `E1410` | Impure or forbidden operation in a law | Call chain from the law to the offending global write, host request, C import, scheduler operation, or identity use (reusing `PurityAnalysis`'s call-chain formatting). |
| `E1411` | Unguarded potential trap in a total equality claim | Highlights the exact arithmetic, index, cast or nil operation that may trap and suggests adding a `given` precondition or using `outcome:`. |
| `E1412` | Proof failed to discharge `no_trap` / `traps` | Shows the remaining unhandled `Outcome.Trap` branch. |
| `E1413` | Termination check failed | Highlights the recursive call site and explains why no structural subterm or declared `measure:` strictly decreased. |
| `E1414` | Cyclic law dependency | Prints the cycle `LawA -> LawB -> LawA`. |
| `E1415` | Normalization fuel exhausted | Names the step budget (`--proof-fuel`) and the most frequently unfolded symbol. |
| `E1416` | Abstract law in a closed (`const` / `#Final`) unit | Explains why a closed unit cannot leave a law unproven. |
| `E1417` | Unencapsulated mutable field in `law.invariant` | Points to the `export let` field and suggests unexported visibility or a `| get:` / guarding `| set:` accessor. |
| `E1418` | Mutable reference escape (`&T`) of an invariant field | Points to the escaping reference binding or return. |
| `E1419` | Independent `--verdict` kernel check failed | Internal elaborator-versus-kernel discrepancy diagnostic (never silently passes). |
| `E1420` | `| axiom;` outside the trusted `Std` library | Mirrors `E0780` (`kernel` outside `Std`). |
| `E1421` | Law feature belongs to a later phase | Names the unavailable feature, including trusted axioms, and its planned phase when assigned. |

A construct that belongs to a later phase is rejected in 0.3.1 with a diagnostic that names the phase's feature as not supported in this release; it is never accepted unchecked.

### 6.2 CLI commands and workflows

```text
# Fast proof-only verification loop (skips optimization and circuit synthesis)
neura prove app.nra
neura prove --laws laws.nra proofs.nra app.nra

# Cross-verify every proof certificate against the minimal standalone micro-kernel
neura prove --verdict --laws laws.nra proofs.nra app.nra

# Tune deterministic reduction fuel and output machine-readable verdicts (human|short|json)
neura prove --proof-fuel 2000000 --error-format json app.nra

# Full check/build with the project-wide law gate (the build fails if any law fails or is incomplete)
neura check --laws laws.nra proofs.nra app.nra
neura build --laws laws.nra proofs.nra app.nra -o app.nvr

# Insert let.proof skeleton stubs for missing inherited law obligations
neura check --fix derived.nra

# Export documented law statements (omitting proof bodies by default) in API docs
neura docs app.nra -o docs.json
```

#### Verdict output (`neura prove`)

```text
  PROVEN   Peano.AddZeroLeft            (1 step, 0.2 ms)
  PROVEN   Peano.AddZeroRight           (14 steps, 0.4 ms)
  PROVEN   BinaryCodec.RoundTrip        (4 steps, 0.2 ms)
  FAILED   BrokenCodec.RoundTrip        (at broken-codec.nra:19:21, E1401)

Verdict: SOME LAWS FAILED (3 proven, 1 failed)
```

---

## 7. Phased rollout plan

| Phase | Release | Scope | Key deliverables |
| --- | --- | --- | --- |
| **Phase 1: core pure laws and module inheritance** | **0.3.1** | Module-level `law (...)`, `| proof:`, `let.proof`; `bool`, enums (fieldless and payload), tuples, `#Immutable` classes; `{==}`, `rewrite` (with motive), `match` and structural induction, `have`, `calc`, `absurd`, `contradiction`, `inject`, `split`, `witness`, `decide`; termination and DAG cycle checking; Proof Core, normalization kernel and complete proof erasure; `neura prove` and the `E14xx` diagnostics. | The Bend-equivalent core over pure Neura modules and inheritance chains, zero runtime overhead, `.nir` 1.16 persistence. |
| **Phase 2: machine numbers, traps and `Std.Proof`** | Roadmap | `Outcome<T>` trap modeling (`no_trap`, `traps`, `outcome:`); exact checked integer (`int8..uint64`), bitvector, string, and IEEE 754 float bit-identity semantics; trusted `| axiom;` lemmas in `Std.Proof.*` audited against `docs/lang/Numbers.md` and circuit specifications; well-founded `measure:` induction. | Honest proofs over real fixed-width Neura arithmetic and floats without overflow or NaN unsoundness. |
| **Phase 3: interfaces, generics and class invariants** | Roadmap | Interface-wide laws (`Comparable`, `Eq`, and the like) and parametric generic law proofs (`<T: I>`); tier-2 encapsulated mutable class invariants (`law.invariant` with `init`, method and setter preservation, and `#Synchronized` monitor synergy); the project-wide build gate through `--laws` and `neura.toml`; the `--verdict` independent micro-kernel verifier; indexed proof-only `prop` types. | Full Liskov enforcement, stateful class invariants, and Bend-style `LAWS`/`PROOF` project workflows. |
| **Phase 4: imperative loops and verified optimization** | Roadmap | `loop_induct` (`invariant` and `variant`) for pure imperative `while`/`for` functions; pure function-value types (`#Pure func(A) -> B`) for higher-order laws; opt-in synergy unlocking the reserved `#Total`, `#NoTrap`, `#Idempotent`, `#Commutative`, `#Associative`, `#Invariant` tags in `src/neura-ir/tags.def` and the optimizer. | Proofs over idiomatic imperative Neura code and mechanically grounded optimizer facts. |

### What 0.3.1 must demonstrate

Phase 1 is complete only when each of these is tested:

- **Checked proofs through separate compilation.** A law proven in one unit is used by a proof in another unit through a prebuilt `.nir`, and its certificate is re-verified there. An abstract law crossing a `.nir` boundary must be fulfilled by the importer, and an override of a function in an imported law's footprint triggers the re-check.
- **Rejection of invalid proofs.** For every Phase 1 primitive there is a rejected proof with the expected `E14xx` code and span, including a wrong `{==}`, a wrong motive, a non-exhaustive `match`, a non-decreasing recursive call, a law cycle, a tampered certificate in a `.nir` file, and an unproven law in `neura build`.
- **Rejection of unsupported operations.** An impure call, a host or C call, a scheduler operation, an identity operation, an operation that may trap in a total equality claim, and each construct of Phases 2 to 4 is refused with a diagnostic. None is accepted unchecked.
- **Erasure before optimization and circuit conversion.** A program with laws and the same program with its laws removed give byte-identical optimized NeuraIR bodies and byte-identical `.nvr` images, apart from the optional `LAWS` metadata section when it is requested.

---

## 8. Decisions

The proposal recorded six open design questions. The owner settled them on 2026-11-21.

| # | Question | Decision |
| --- | --- | --- |
| 1 | Direction of `rewrite` | Left to right by default: `rewrite e in M(_);` turns `M(lhs)` into `M(rhs)`. `rewrite.rev` is the reverse. |
| 2 | Nullability of class quantifiers | `for x: MyClass;` quantifies over non-nil instances. `for? x: MyClass;` includes `nil`. |
| 3 | `rewrite e;` shorthand | Phase 1 requires an explicit motive. The shorthand is deferred. |
| 4 | Inductive proof predicates | Phase 1 uses boolean predicates and checked witnesses. Indexed proof-only `prop` types are deferred to Phase 3. |
| 5 | Override of a proof dependency | When an override changes a function in an inherited proof's footprint, the compiler re-checks the inherited proof. If it fails, the derived unit must supply `let.proof`, and the inherited statement stays intact. |
| 6 | Project manifest | In Phase 3, project-wide laws are listed in the existing `neura.toml` and are enforced automatically in project builds and in `neura prove`. |
