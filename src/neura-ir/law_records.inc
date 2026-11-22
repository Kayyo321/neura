// Proof-only records for NeuraIR 1.16. These never form executable bodies.
inline constexpr std::uint32_t law_no_binding = static_cast<std::uint32_t>(-1);
inline constexpr std::uint32_t law_wildcard_arm = law_no_binding - 1;

enum class LawTermKind : std::uint8_t {
    Variable, Boolean, Nil, Constructor, Call, Project, If, Not, And, Or, BooleanEqual,
    Equal, All, Either, Empty, Implies, Exists, Forall, NullableForall, Hole, Constant, Construct
};

struct LawTerm {
    LawTermKind kind = LawTermKind::Empty;
    // A value's type, the sides' type for Equal, or the quantified type for
    // Exists/Forall. Logical connectives without a value type carry Void.
    IrType type;
    Span span;
    // Enum variant, function declaration, or projection field, as applicable.
    SymbolId symbol = no_symbol;
    // A fixed module receiver on a call, or no_symbol for lexical dispatch.
    SymbolId receiver = no_symbol;
    // A binder, literal value, or projection index, according to kind.
    std::uint32_t index = 0;
    std::vector<LawTerm> terms{};
};

enum class LawProofKind : std::uint8_t {
    Assumption, Apply, Refl, Block, Rewrite, RewriteReverse, Have, Match, Arm, If,
    Absurd, Contradiction, Inject, Split, Witness, Intro, ChooseLeft, ChooseRight,
    Sym, Trans, Cong, Decide, Calc, Local
};

struct LawProof {
    LawProofKind kind = LawProofKind::Block;
    Span span;
    // Declaration for Apply or enum variant for an Arm.
    SymbolId symbol = no_symbol;
    SymbolId receiver = no_symbol;
    std::uint32_t index = 0;
    // Payload binders for Arm and evidence binders for Inject. An ignored
    // payload has law_no_binding in its position.
    std::vector<std::uint32_t> bindings{};
    std::vector<LawTerm> terms{};
    std::vector<LawProof> proofs{};
};

enum class LawStatus : std::uint8_t { Abstract, Proven, TrustedAxiom };

struct IrLawAbbreviation {
    std::string name;
    LawTerm value;
};

struct IrLawRecord {
    SymbolId law_symbol = no_symbol;
    // Declaring module or, for let.proof, the module supplying the proof.
    SymbolId owner = no_symbol;
    LawStatus status = LawStatus::Abstract;
    // Number of leading for/given binders introduced implicitly at the start
    // of a certificate. Inner quantifiers and implications are explicit.
    std::uint32_t introductions = 0;
    LawTerm statement;
    std::vector<SymbolId> footprint;
    std::optional<LawProof> certificate;
    // Names a fulfillment may use to refer to definitions in its inherited
    // statement. These are proof-only values, validated with the certificate.
    std::vector<IrLawAbbreviation> abbreviations{};
};
