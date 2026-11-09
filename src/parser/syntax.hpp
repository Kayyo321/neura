#pragma once

#include "lexer/lexer.hpp"

#include <memory>
#include <optional>
#include <string>
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
    std::optional<TypeSyntax> bound;
};

struct Parameter {
    Span span;
    std::string name;
    TypeSyntax type;
};

struct Expr {
    enum class Kind {
        Name, Integer, Float, String, Boolean, Unary, Binary, Member, Call,
        Index, Collection, New, Import,
        // `operand as type`: the target type is in `type`.
        Cast,
    };

    Kind kind;
    Span span;
    // Identifier, literal, operator, member name, or qualified import name.
    std::string text;
    // The first operand of Call, Member, and Index is the callee or receiver.
    std::vector<Expr> operands;
    std::vector<TypeSyntax> type_arguments;
    std::optional<TypeSyntax> type;
};

struct Declaration;

struct Statement {
    enum class Kind {
        Binding, Expression, Assignment, Block, If, While, Return, Break, Continue, Destroy,
        // for (init; condition; step) body. `initializer` and `step` hold at
        // most one statement each; `expressions` holds the condition if any.
        For,
        // for (let name: Type in collection) body. `declaration` names the
        // element binding; `expressions` holds the collection.
        ForEach,
    };

    Kind kind;
    Span span;
    std::unique_ptr<Declaration> declaration;
    std::vector<Expr> expressions;
    std::vector<Statement> body;
    std::vector<Statement> alternative;
    std::vector<Statement> initializer;
    std::vector<Statement> step;
};

struct Declaration {
    // An enum's variants are its members, as bindings with only a name.
    enum class Kind { Binding, Function, Class, Module, Enum };

    Kind kind;
    Span span;
    std::string name;
    bool constant{};
    bool exported{};
    bool override_member{};
    bool has_body{};
    // Binding annotation or function return type.
    std::optional<TypeSyntax> type;
    std::optional<TypeSyntax> base;
    std::optional<Expr> initializer;
    std::vector<GenericParameter> generics;
    std::vector<Parameter> parameters;
    std::vector<Declaration> members;
    std::vector<Statement> body;
};

struct ResolvedImport {
    std::string name;
    std::string file;
};

struct ParsedFile {
    std::vector<Statement> statements;
    std::string file;
    std::vector<ResolvedImport> imports;
};

ParsedFile parse(const std::vector<Token>& tokens);

} // namespace neura
