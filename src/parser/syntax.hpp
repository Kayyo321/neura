#pragma once

#include "lexer/lexer.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neura {

enum class CollectionKind { Array, List };

struct Declaration;

struct TypeSyntax {
    Span span;
    std::string name;
    std::vector<TypeSyntax> arguments;
    std::vector<CollectionKind> collections;
    bool reference{};
    // An anonymous class written in a type position, `class ( members )`:
    // the name is "[anonymous class]" and this is its class declaration.
    // Copies of the type share the declaration, which identifies the
    // occurrence (Types.md, Anonymous classes).
    std::shared_ptr<Declaration> anonymous{};
};

struct GenericParameter {
    Span span;
    std::string name;
    std::vector<TypeSyntax> bounds;
};

// One argument of a tag: `literal` or `name: literal` (docs/lang/Tags.md).
struct TagArgumentSyntax {
    enum class Kind { String, Integer, Float, Boolean, Name, List };

    Kind kind;
    // The value, without the name of a named argument.
    Span span;
    // The argument's name when it is written `name: value`, otherwise empty.
    std::string name;
    std::optional<Span> name_span{};
    // A string's decoded text; the spelling of a number, `true` or `false`,
    // or a qualified name; empty for a list.
    std::string text;
    // The items of a list, which are not lists themselves.
    std::vector<TagArgumentSyntax> items{};
};

// One tag written before a declaration. A group gives one TagSyntax per
// tag, with the same representation for dot and hash spellings.
struct TagSyntax {
    Span span;
    Span name_span;
    std::string name;
    std::vector<TagArgumentSyntax> arguments;
};

struct Expr;

struct Parameter {
    Span span;
    std::string name;
    TypeSyntax type;
    bool variadic{};
    std::vector<TagSyntax> tags{};
    // A constant default, `name: Type = value` (docs/design/Calls030.md),
    // and the span from the '=' through the value. Lowering checks it.
    std::shared_ptr<Expr> default_value{};
    std::optional<Span> default_span{};
};

struct Declaration;
struct MatchArm;
struct SelectArm;
struct Statement;
struct HtmlFactorySyntax;
struct StylesheetSyntax;

struct Expr {
    enum class Kind {
        Name, Integer, Float, String, Boolean, Unary, Binary, Member, Call,
        Index, Collection, Range, New, Import,
        // `operand as type`: the target type is in `type`.
        Cast, NamedVariant, Match,
        ComparisonChain, Conditional, PrefixUpdate, PostfixUpdate,
        Lambda, Nil, TypeTest, TypeApplication,
        // `spawn call` (Concurrency.md section 4.4): the call is operand 0.
        // The span starts at the contextual keyword, which is five bytes long.
        Spawn, HtmlFactory, Stylesheet,
        // `$"text {hole:spec}"` (docs/design/Calls030.md): the operands are
        // the hole expressions in written order, nested width and precision
        // holes included, and `interpolation` describes them.
        Interpolation,
        // `nameof(path)`: operand 0, when written, is the path, which is
        // not evaluated.
        NameOf,
        // docs/design/Matching030.md. `receiver?.member`: operand 0 is the
        // receiver and `text` the member; a call `receiver?.m(arguments)`
        // is a Call whose callee is an OptionalMember. `left ?? right` has
        // its two operands. `operand?` is a Propagate. `operator_span` is
        // the `?.`, `??` or `?` token.
        OptionalMember, Coalesce, Propagate,
        // `new with(source) { name: value, ... }` (docs/design/Records030.md,
        // Copy construction): operand 0 is the source and each later operand
        // a replacement, whose field name is in `labels` and `label_spans`
        // ("" for the source). The span starts at `new`.
        Copy,
        // docs/design/Lifetime030.md. `iterator(parameters) -> T { body }`:
        // `lambda` is the declaration, with `generator` set and the result
        // type `[generator]` of the element type.
        Generator,
        // `typeof(operand)` (docs/design/Expressions030.md, typeof): operand 0
        // is the operand read as an expression, when it parses as one, and
        // `type` the operand read as a type, when it parses as one; lowering
        // decides which it is. The span starts at `typeof`.
        TypeOf,
    };

    Kind kind;
    Span span;
    // Identifier, literal, operator, member name, or qualified import name.
    std::string text;
    // The first operand of Call, Member, and Index is the callee or receiver.
    std::vector<Expr> operands;
    std::vector<TypeSyntax> type_arguments;
    std::optional<TypeSyntax> type;
    // True for an 'and' comparison chain and false for an 'or' chain.
    bool chain_conjunction{};
    std::shared_ptr<Declaration> lambda{};
    std::vector<std::string> field_names{};
    // The span of each name in `field_names`, for a NamedVariant
    // (`Enum.Variant { name: value }`); the editor renames them.
    std::vector<Span> field_spans{};
    std::shared_ptr<Statement> match{};
    // Artifact imports carry a quoted path and an optional module name in text.
    std::optional<std::string> import_path{};
    // `import:c("header.h", "lib.a", ...)`: the header and then the libraries,
    // as written. The loader generates the module (ForeignInterop.md).
    std::vector<std::string> c_import{};
    // The opening parenthesis of a Call's argument list.
    std::optional<Span> open{};
    // The labels of named arguments (docs/design/Calls030.md), parallel to
    // `operands` of a Call or a New: empty when no argument is named, and
    // otherwise "" for the callee and for each positional argument.
    std::vector<std::string> labels{};
    std::vector<Span> label_spans{};
    std::shared_ptr<const struct InterpolationSyntax> interpolation{};
    std::optional<Span> operator_span{};
    std::shared_ptr<HtmlFactorySyntax> html_factory{};
    std::shared_ptr<StylesheetSyntax> stylesheet{};
    // docs/design/Expressions030.md, Negation words. A Unary "!" written
    // `not`, either as a prefix (`not x`) or after `is` (`x is not T`, whose
    // operand is the TypeTest); `operator_span` is the word `not`.
    bool spelled_not{};
    bool is_not{};
    // A node the parser made for a source form that has no token of its
    // own: the negation of a `return ... unless guard` (docs/design/
    // Expressions030.md, Return forms). Its span is its operand's.
    bool implicit{};
    // docs/design/Expressions030.md, Narrowing. A TypeTest written
    // `x is name: T` binds `binding`, written at `narrowing`. The parser
    // accepts it only as the whole condition of an `if` or a top-level
    // conjunct of the `&&` chain that is the whole condition.
    std::string binding{};
    std::optional<Span> narrowing{};
    // docs/design/Expressions030.md, Character literals. An Integer written
    // as a character literal, whose text is its value in decimal; its type
    // without an expected integer type is `byte`.
    bool character{};
    // docs/design/Expressions030.md, Ranges with `..=` and String chains. A
    // Range holds two or more bounds and then, when `step_colon` is set, the
    // step written after that colon. `range_operators` are the `..` and `..=`
    // tokens in written order, and `range_inclusive` says which of them is
    // `..=`. Lowering decides from the type of the first bound whether the
    // node is an integer range or a string chain.
    std::vector<Span> range_operators{};
    std::vector<bool> range_inclusive{};
    std::optional<Span> step_colon{};
    // An expression written in parentheses, which a chain keeps as one
    // operand, and a range written in braces, `{a .. b}`.
    bool grouped{};
    bool braced{};
    // A numeric suffix fixes this literal's primitive type.
    std::string_view literal_type{};
};

// The pattern and holes of an interpolated string. The pattern is the
// format string the holes are formatted with: its text with braces doubled,
// and each hole as `{index}` or `{index:specification}`, where a nested hole
// in the specification is `{index}` too.
struct InterpolationHole {
    // The operand of the value, and the operands of its nested holes.
    std::size_t operand;
    std::vector<std::size_t> nested;
    // The specification as the pattern writes it, and where it is written.
    std::string specification;
    std::optional<Span> specification_span;
    // Whether the specification is one piece of text, so that an offset in
    // it is an offset in the source.
    bool plain_specification = true;
};

struct InterpolationSyntax {
    std::string pattern;
    std::vector<InterpolationHole> holes;
};

struct Statement {
    enum class Kind {
        Binding, Expression, Assignment, Block, If, While, Return, Break, Continue, Destroy,
        // for (init; condition; step) body. `initializer` and `step` hold at
        // most one statement each; `expressions` holds the condition if any.
        For,
        // for (let name: Type in collection) body. `declaration` names the
        // element binding; `expressions` holds the collection.
        ForEach, Destructure, Match, Do,
        // scope { body } and scope (name) { body }. `bindings` holds the
        // handle name when one is given; `keyword` is the contextual word.
        Scope,
        // select { arms }. `keyword` is the contextual word.
        Select,
        // docs/design/Lifetime030.md. `defer call;`: the call is
        // expressions[0]. `using (name = value) { body }`: `bindings` holds
        // the name and expressions[0] the acquisition. `yield value;` in a
        // generator body: the value is expressions[0]. `keyword` is the
        // contextual word.
        Defer, Using, Yield,
    };

    Kind kind;
    Span span;
    std::unique_ptr<Declaration> declaration;
    std::vector<Expr> expressions;
    std::vector<Statement> body;
    std::vector<Statement> alternative;
    // The header binding of an `if`, or the initializer of a three-part `for`.
    std::vector<Statement> initializer;
    std::vector<Statement> step;
    std::vector<Token> bindings;
    bool constant_bindings{};
    std::vector<MatchArm> arms;
    std::vector<SelectArm> select_arms;
    // The contextual keyword of a scope or select statement, or the word
    // `unless` of the `if` that a guarded return stands for (docs/design/
    // Expressions030.md, Return forms).
    Span keyword{};
    // Empty for other statements. Holds the operator for an assignment.
    std::string assignment_operator{};
    // Documentation shared by all bindings in a destructuring declaration.
    std::shared_ptr<const DocComment> documentation{};
    // `match.is (subject) { arms }` (docs/design/Matching030.md).
    bool match_is{};
    // An `if` whose condition narrows (docs/design/Expressions030.md,
    // Narrowing): `bindings` holds the names it binds, in written order, and
    // `constant_bindings` is set. They are visible in `body`; for a negated
    // narrowing, `if (x is not y: T)`, in `alternative` instead (and after
    // the `if` when `body` cannot complete normally).
    bool negated_narrowing{};
    // A destructuring declaration's tuple annotation, `let [a, b]: [A, B]`.
    // Its `expressions` hold one initializer, or one per name.
    std::optional<TypeSyntax> binding_type{};
    // A Block written `static scope { ... }` (Modules.md, Startup blocks).
    // `keyword` spans the two words `static scope`.
    bool startup{};
};

// The pattern of a match arm (docs/design/Matching030.md). In a `match`,
// a whole pattern is `_`, a variant pattern or a tuple pattern, and a
// payload or element position holds a sub-pattern: `_`, a binding, a
// variant pattern or a tuple pattern. In a `match.is`, a pattern is `_`,
// `nil`, a type, or a binding with a type.
struct PatternSyntax {
    enum class Kind { Wildcard, Binding, Variant, Tuple, Type, Nil };

    Kind kind = Kind::Wildcard;
    Span span;
    // A binding's name, or a variant pattern's qualified name.
    std::string name;
    // A binding's name as written.
    Span name_span{};
    // A variant pattern's payload, or a tuple pattern's elements.
    std::vector<PatternSyntax> elements{};
    // Whether a variant pattern has a payload list, and whether it is the
    // braced form, where `fields` names each element's field.
    bool payload{};
    bool named{};
    std::vector<std::string> fields{};
    std::vector<Span> field_spans{};
    // The type of a Type pattern, or of a binding `name: Type` in a `match.is`.
    std::optional<TypeSyntax> type{};
};

// A proposition in a law statement or proof. The expression operands are
// ordinary Neura syntax; lowering assigns their proof-domain types.
struct PropositionSyntax {
    enum class Kind { Equality, Boolean, All, Either, Not, NoTrap, Traps, Outcome, For, Given };

    Kind kind;
    Span span;
    std::vector<Expr> expressions;
    std::optional<TypeSyntax> type;
    std::vector<PropositionSyntax> propositions;
    std::optional<Token> trap_class;
    std::vector<Parameter> parameters;
};

struct LawItemSyntax {
    enum class Kind { For, Exists, Given, Last, Measure, Claim, Holds, NoTrap, Traps, Outcome, Refutes };

    Kind kind;
    Span span;
    Span keyword;
    std::vector<Parameter> parameters;
    bool nullable{};
    std::optional<Token> binding;
    std::optional<TypeSyntax> type;
    std::vector<Expr> expressions;
    std::optional<PropositionSyntax> proposition;
    std::optional<Token> trap_class;
};

struct ProofSyntax;

struct ProofMatchArmSyntax {
    Span span;
    PatternSyntax pattern;
    // One proof expression or block.
    std::vector<ProofSyntax> body;
};

struct ProofSyntax {
    enum class Kind {
        Block, Reflexivity, Decide, Term, Rewrite, Have, Last, Witness, Intro, Inject,
        Absurd, Contradiction, Calc, CalcStep, Split, LoopInduct, Match, If, ChooseLeft, ChooseRight,
    };

    Kind kind;
    Span span;
    Span keyword;
    std::vector<ProofSyntax> children;
    std::vector<ProofMatchArmSyntax> arms;
    std::optional<Expr> expression;
    std::optional<PropositionSyntax> proposition;
    std::optional<TypeSyntax> type;
    std::vector<Token> bindings;
    bool reverse{};
};

struct LawSyntax {
    std::vector<LawItemSyntax> items;
    std::optional<ProofSyntax> proof;
    std::optional<Span> axiom;
    std::optional<Span> todo;
    std::optional<Span> invariant;
    std::optional<Span> override_word;
    // The qualified target of `let.proof`, in source order.
    std::vector<Token> target;
    Span keyword;
    Span open;
    Span close;
    std::optional<Span> attachment;
};

struct MatchArm {
    Span span;
    // An arm in the form of 0.2 (one level of bindings, no guard) also
    // keeps that form here: the variant's qualified name or "_", one token
    // per payload position, and the field names of the braced form. Other
    // arms set `general`; their `variant` is the whole pattern's variant
    // name, "_", or empty, and `bindings` lists every name they bind, in
    // written order, so that walkers that only scope names stay correct.
    std::string variant;
    std::vector<Token> bindings;
    std::vector<std::string> fields;
    bool named{};
    std::vector<Statement> body;
    std::optional<Expr> value;
    // The whole pattern, set for every arm.
    std::shared_ptr<const PatternSyntax> pattern{};
    // `pattern if guard => result`.
    std::optional<Expr> guard{};
    bool general{};
};

// One arm of a select statement: `[let name =] operation => { body }`, or
// the default arm `_ => { body }`, which has no operation.
struct SelectArm {
    Span span;
    std::optional<Token> binding;
    bool constant_binding{};
    std::optional<Expr> operation;
    std::vector<Statement> body;
};

// An accessor clause after an exported instance field's ';' (Types.md,
// Accessors): `| get: expression;`, `| get: { block };`, `| set: statement;`,
// or `| set: { block };`. `get`, `set`, and `value` are contextual words.
struct AccessorClause {
    bool setter{};
    // From the '|' through the clause's ';'.
    Span span;
    // The word `get` or `set`.
    Span keyword;
    // A getter's expression form. Empty for a block body or a setter.
    std::optional<Expr> expression;
    // A setter's single statement, or the statements of a block body.
    std::vector<Statement> body;
    bool block{};
    // `| get;` and `| set;` without a body: a clause of an interface property
    // (docs/design/Records030.md, Interface properties).
    bool bodyless{};
    // The words written after the clause's word, `.virtual` or `.override`
    // (Records030.md, Virtual accessors), each with its span. More than one
    // is an error that lowering reports.
    enum class Dispatch { None, Virtual, Override };
    Dispatch dispatch{Dispatch::None};
    std::vector<std::pair<std::string, Span>> dispatch_words{};
};

// A record class (docs/design/Records030.md, Records). The parser has built
// its fields, one `export last` field per component, and its generated
// constructor `init = func(components) => set`, already expanded.
struct DeriveMapping {
    bool variant{};
    std::vector<std::string> parts;
    std::vector<Span> part_spans;
    Span path;
    std::string wire;
    Span wire_span;
};

struct DeriveSyntax {
    TypeSyntax generator;
    std::vector<TypeSyntax> arguments;
    std::vector<DeriveMapping> mappings;
    std::optional<Span> close_brace;
};

struct RecordSyntax {
    // The word `record`, and the ')' that ends the components.
    Span keyword;
    Span close;
    // Parts a record does not take, in source order, each with the message
    // of its E0570; lowering reports the first.
    std::vector<std::pair<Span, std::string>> problems{};
};

// A class hook clause, written after the '(' of a declared class and before
// its first member (docs/design/Hooks030.md, Class hook clauses):
// `| get: expression;`, `| set.override: { block };`. `get`, `set`, and
// `value` are contextual words; `override` after the '.' is the keyword.
struct HookClause {
    bool setter{};
    // From the '|' through the clause's ';'.
    Span span;
    // The word `get` or `set`.
    Span keyword;
    // The word `override` of `.override`, when written.
    std::optional<Span> override_word{};
    // The expression form. Empty for a block body.
    std::optional<Expr> expression{};
    // The statements of a block body.
    std::vector<Statement> body{};
    bool block{};
    // The block's closing '}', where a path that falls through ends.
    Span close{};
};

struct Declaration {
    // An enum's variants are its members. Their parameters describe the payload.
    enum class Kind { Binding, Function, Class, Module, Enum, Interface, Law, LawProof };

    Kind kind;
    Span span;
    std::string name;
    bool constant{};
    // `last`: an assign-once binding. It is also `constant`; without an
    // initializer it is deferred and assigned exactly once later.
    bool last{};
    bool exported{};
    bool override_member{};
    bool promote_import{};
    bool static_member{};
    bool singleton{};
    bool has_body{};
    // Binding annotation or function return type.
    std::optional<TypeSyntax> type;
    std::optional<TypeSyntax> base;
    std::optional<Expr> initializer;
    std::vector<GenericParameter> generics;
    std::vector<Parameter> parameters;
    bool named_payload{};
    std::vector<Declaration> members;
    std::vector<Statement> body;
    std::optional<std::pair<std::string, std::uint32_t>> host_binding{};
    // `callback("service", version)` after a block body: a function the
    // runtime may call during a host request (trusted library only).
    std::optional<std::pair<std::string, std::uint32_t>> host_callback{};
    // `scheduler("operation")` in place of a body: a function the NVM
    // scheduler implements (trusted library only; Concurrency.md section 10.1).
    std::optional<std::string> scheduler_binding{};
    // `kernel("neura.area.name", version)` in place of a body: a function a
    // built-in C kernel implements (trusted library only; Kernels.md).
    std::optional<std::pair<std::string, std::uint32_t>> kernel_binding{};
    std::vector<TypeSyntax> interfaces{};
    // Additional signatures in an `or func` declaration.
    std::vector<Declaration> overloads{};
    // The target of a class `let.as` declaration. Each `or as` is its own member.
    std::optional<TypeSyntax> cast_target{};
    std::shared_ptr<const DocComment> documentation{};
    std::optional<Span> name_span{};
    // Accessor clauses after the member's ';', in source order.
    std::vector<AccessorClause> accessors{};
    // The class hook clauses after a declared class's '(', in source order.
    std::vector<HookClause> hooks{};
    // The startup blocks written among a module's members, in source order
    // (Modules.md, Startup blocks). Each is a Block statement with `startup`.
    std::vector<Statement> startup_blocks{};
    // The tags written before the declaration, in source order. They apply
    // to every branch of an `or func` group.
    std::vector<TagSyntax> tags{};
    // True when the function's body was written `=> expression`.
    bool expression_body{};
    // The `set` of a constructor written `init = func(...) => set`
    // (ObjectLifecycle.md, Constructor shorthand). The parser has expanded
    // it into `body`: an optional `super.init()` and one store to the
    // field of each parameter, through the receiver `$instance`, a name no
    // source can declare. Each store has the span of its parameter.
    std::optional<Span> set_shorthand{};
    // A lambda whose parameters are all written without types, such as
    // `func(a, b) => a + b`: each parameter's type is "[untyped]" and comes
    // from the expected function type (docs/design/Calls030.md).
    bool untyped_parameters{};
    // Whether the result type was written with `->`.
    bool result_written{};
    // `let.extension name = func(receiver: T, ...)` declares an extension
    // function, and `let.extensions alias = import:Module;` is an import
    // that enables a module's extension functions (docs/design/Calls030.md).
    // `extension_word` is the word after `let.`.
    bool extension{};
    bool extension_set{};
    std::optional<Span> extension_word{};
    // A record (docs/design/Records030.md): a class whose members the parser
    // built from its components. Lowering adds its value members.
    std::shared_ptr<const RecordSyntax> record{};
    std::shared_ptr<const DeriveSyntax> derive{};
    // The word `implements` of a class or of a module's conformance list
    // (`module implements I<T> (`, trusted companions only).
    std::optional<Span> implements_span{};
    // A member that lowering generated and no source spells, such as a
    // record's value members. Tools skip it where they show written source.
    bool generated{};
    // A generator, `iterator(parameters) -> T { body }`
    // (docs/design/Lifetime030.md): `type` is `[generator]` with the element
    // type as its argument, which lowering reads as Sequence<T>.
    bool generator{};
    std::optional<Span> generator_keyword{};
    // A proposition and its attached proof, or a `let.proof` fulfillment.
    std::shared_ptr<LawSyntax> law{};
};

struct ResolvedImport {
    std::string name;
    std::string file;
    std::string target_module{};
    bool artifact{};
    std::string artifact_path{};
};

struct ParsedFile {
    std::vector<Statement> statements;
    std::string file;
    std::vector<ResolvedImport> imports;
    std::string library_path{};
    std::optional<Span> untyped_variadic{};
};

// When requested, records the byte offsets of tag sigils for source tools.
// A speculative parse may record an offset more than once.
ParsedFile parse(const std::vector<Token>& tokens, std::vector<std::size_t>* tag_sigils = nullptr);
// Retains valid declarations around recovered syntax errors for editor use.
ParsedFile parse_recovering(const std::vector<Token>& tokens, std::vector<Diagnostic>& diagnostics,
                            std::vector<std::size_t>* tag_sigils = nullptr);

} // namespace neura
