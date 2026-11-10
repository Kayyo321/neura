#pragma once

#include "util/diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace neura {

struct ParsedFile;

using SymbolId = std::size_t;
inline constexpr SymbolId no_symbol = static_cast<SymbolId>(-1);

enum class TypeKind {
    Void, Bool, String, Integer, Float, Array, List, Object, TypeParameter, Module, Enum, Callable,
    Tuple
};

struct IrType {
    TypeKind kind = TypeKind::Void;
    std::string name;
    SymbolId symbol = no_symbol;
    std::vector<IrType> arguments;

    bool operator==(const IrType&) const = default;
};

// Callable references to named declarations and built-in methods have no
// structural signature. Function values list parameter types then the result.
inline bool is_function_value(const IrType& type) {
    return type.kind == TypeKind::Callable && type.symbol == no_symbol && !type.arguments.empty();
}

enum class SymbolKind {
    File, Module, Class, Alias, Import, Field, Function, Parameter, Local, Global,
    TypeParameter, Enum, Variant, Interface
};

struct IrHostBinding {
    std::string name;
    std::uint32_t version = 0;
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
    // Only enum variants have payload types, in declaration order.
    std::vector<IrType> payload_types{};
    // Set on files loaded from the trusted standard library root.
    std::string library_path{};
    std::optional<IrHostBinding> host_binding{};
    // Implemented or extended interfaces, with their type arguments.
    std::vector<IrType> interfaces{};
    // The first bound stays in bound for compatibility with older NIR.
    std::vector<IrType> additional_bounds{};
};

// Class-owned namespace for class-level storage and receiver-free functions.
inline constexpr const char* static_scope_name = "$static";

inline bool is_static_scope(const IrSymbol& symbol) {
    return symbol.kind == SymbolKind::Module && symbol.name == static_scope_name;
}

enum class IrOp {
    Initialize, Store, Load, Reference, Receiver, Field, Index, Collection,
    Integer, Float, String, Boolean, Unary, Binary, ShortCircuit,
    Call, VirtualCall, ExternalCall, BaseInitialize, Allocate, Length, Append, RemoveAt,
    Return, Block, Branch, Loop, Break, Continue, Destroy, Discard,
    // NeuraIR 1.1.
    Convert, CollectionVersion, Fail,
    // NeuraIR 1.2. Projections use a zero-based decimal index in text.
    Tuple, Project, EnumConstruct, EnumTag, EnumProject, ComparisonChain,
    // ScopedValue has local statements followed by a result expression in
    // body. SelectValue evaluates one result region based on its operand.
    ScopedValue, SelectValue,
    // NeuraIR 1.3. Specialization gives structural function values concrete
    // object layouts and rewrites these operations to allocation and dispatch.
    MakeClosure, CallClosure,
    TextLength, TextByteAt, TextSlice, TextFromUtf8, TextEqual, ReinterpretFloat, TrapMessage,
    Nil, ClassHash, ClassText, ClassIs, ClassCast,
    ValueEquals, ValueHash, ValueText,
    // NeuraIR 1.4. Allocate an array or list filled with one value.
    RepeatCollection
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
