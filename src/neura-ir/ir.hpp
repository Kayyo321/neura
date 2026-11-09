#pragma once

#include "util/diagnostic.hpp"

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace neura {

struct ParsedFile;

using SymbolId = std::size_t;
inline constexpr SymbolId no_symbol = static_cast<SymbolId>(-1);

enum class TypeKind {
    Void, Bool, String, Integer, Float, Array, List, Object, TypeParameter, Module, Enum, Callable
};

struct IrType {
    TypeKind kind = TypeKind::Void;
    std::string name;
    SymbolId symbol = no_symbol;
    std::vector<IrType> arguments;

    bool operator==(const IrType&) const = default;
};

enum class SymbolKind {
    File, Module, Class, Alias, Import, Field, Function, Parameter, Local, Global,
    TypeParameter, Enum, Variant
};

struct IrSymbol {
    SymbolKind kind;
    std::string name;
    Span span;
    SymbolId owner = no_symbol;
    SymbolId base = no_symbol;
    SymbolId overrides = no_symbol;
    IrType type;
    std::vector<SymbolId> parameters;
    std::vector<SymbolId> generics;
    std::optional<IrType> bound;
    bool exported = false;
    bool constant = false;
    bool external = false;
    bool has_body = false;
};

enum class IrOp {
    Initialize, Store, Load, Reference, Receiver, Field, Index, Collection,
    Integer, Float, String, Boolean, Unary, Binary, ShortCircuit,
    Call, VirtualCall, ExternalCall, BaseInitialize, Allocate, Length, Append, RemoveAt,
    Return, Block, Branch, Loop, Break, Continue, Destroy, Discard,
    // NeuraIR 1.1.
    Convert, CollectionVersion, Fail
};

struct IrNode {
    IrOp op;
    IrType type;
    Span span;
    SymbolId symbol = no_symbol;
    std::string text;
    // Operands are evaluated left to right. Store uses its first operand as a
    // destination. ShortCircuit evaluates its right operand in body lazily.
    // A Loop keeps its condition as its operand, its statements in body, and
    // an optional latch in alternative: the latch runs after the body
    // completes and when the body continues, before the condition is tested
    // again. Convert changes its operand to the node's numeric type. Fail
    // ends the run with the failure its text names.
    std::vector<IrNode> operands;
    std::vector<IrNode> body;
    std::vector<IrNode> alternative;
    std::vector<IrType> type_arguments;
};

struct IrBody {
    SymbolId owner;
    std::vector<IrNode> instructions;
};

struct IrProgram {
    std::vector<IrSymbol> symbols;
    std::vector<IrBody> bodies;
};

IrProgram lower(const std::vector<ParsedFile>& files);
void print_ir(std::ostream& output, const IrProgram& program);

} // namespace neura
