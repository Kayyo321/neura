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

struct TypeSyntax {
    Span span;
    std::string name;
    std::vector<TypeSyntax> arguments;
    std::vector<CollectionKind> collections;
};

struct GenericParameter {
    Span span;
    std::string name;
    std::vector<TypeSyntax> bounds;
};

struct Parameter {
    Span span;
    std::string name;
    TypeSyntax type;
};

struct Declaration;
struct MatchArm;
struct Statement;

struct Expr {
    enum class Kind {
        Name, Integer, Float, String, Boolean, Unary, Binary, Member, Call,
        Index, Collection, New, Import,
        // `operand as type`: the target type is in `type`.
        Cast, NamedVariant, Match,
        ComparisonChain, Conditional, PrefixUpdate, PostfixUpdate,
        Lambda, Nil, TypeTest, TypeApplication,
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
    std::shared_ptr<Statement> match{};
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
    };

    Kind kind;
    Span span;
    std::unique_ptr<Declaration> declaration;
    std::vector<Expr> expressions;
    std::vector<Statement> body;
    std::vector<Statement> alternative;
    std::vector<Statement> initializer;
    std::vector<Statement> step;
    std::vector<Token> bindings;
    bool constant_bindings{};
    std::vector<MatchArm> arms;
    // Empty for other statements. Holds the operator for an assignment.
    std::string assignment_operator{};
};

struct MatchArm {
    Span span;
    std::string variant;
    std::vector<Token> bindings;
    std::vector<std::string> fields;
    bool named{};
    std::vector<Statement> body;
    std::optional<Expr> value;
};

struct Declaration {
    // An enum's variants are its members. Their parameters describe the payload.
    enum class Kind { Binding, Function, Class, Module, Enum, Interface };

    Kind kind;
    Span span;
    std::string name;
    bool constant{};
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
    std::vector<TypeSyntax> interfaces{};
};

struct ResolvedImport {
    std::string name;
    std::string file;
    std::string target_module{};
};

struct ParsedFile {
    std::vector<Statement> statements;
    std::string file;
    std::vector<ResolvedImport> imports;
    std::string library_path{};
};

ParsedFile parse(const std::vector<Token>& tokens);

} // namespace neura
