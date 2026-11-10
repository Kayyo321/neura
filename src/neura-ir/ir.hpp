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
struct LinkedImports;

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

enum class VariadicKind { None, Typed, Untyped };

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
    // Source-level signatures needed when another compilation imports this IR.
    SymbolId overload_group = no_symbol;
    VariadicKind variadic = VariadicKind::None;
    bool user_cast = false;
    SymbolId singleton_instance = no_symbol;
};

// Class-owned namespace for class-level storage and receiver-free functions.
inline constexpr const char* static_scope_name = "$static";

inline bool is_static_scope(const IrSymbol& symbol) {
    return symbol.kind == SymbolKind::Module && symbol.name == static_scope_name;
}

inline bool is_static_initializer(const IrSymbol& symbol) {
    return symbol.kind == SymbolKind::Function && symbol.name == "$static_init";
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
    RepeatCollection,
    // NeuraIR 1.7. Materialized into shared storage before specialization.
    ReferencePlace,
    // NeuraIR 1.9 (docs/lang/Concurrency.md). TaskScope is a statement whose
    // body runs inside a task scope: every exit from the body, including
    // return, break, and continue, first waits for the tasks the scope owns.
    // Its symbol is the constant local bound to the scope handle, if any.
    // Spawn starts its operand, a function value that takes no arguments, as
    // a task of the innermost enclosing TaskScope of the same function and
    // yields the uint64 task identity. SchedulerCall asks the NVM scheduler
    // for the operation its text names; the scheduler defines each
    // operation's arguments and result.
    TaskScope, Spawn, SchedulerCall
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
    // NeuraIR 1.9. A poll counts one language operation toward the running
    // task's quantum: on a Loop it is counted each time the back edge is
    // taken, and on a Call, VirtualCall, or CallClosure when the call is
    // made. insert_polls sets it across a program that contains a Spawn.
    bool poll = false;
};

struct IrBody {
    SymbolId owner;
    std::vector<IrNode> instructions;
};

struct IrStaticConstant {
    SymbolId field;
    IrNode value;
};

// What a C import needs linked into the image (ForeignInterop.md): the
// header, the static libraries and object files given with it, the C source
// of wrappers for its static inline functions, and the symbols its
// generated module calls. The source loader fills these; the tool links them.
struct IrNativeUnit {
    std::string header;
    std::vector<std::string> libraries;
    std::string shim;
    std::vector<std::string> symbols;
};

// A linked shared object for the image's NATV section (NIR 1.8).
struct IrNativeLibrary {
    std::string triple;
    std::vector<std::uint8_t> object;
    std::vector<std::string> symbols;

    bool operator==(const IrNativeLibrary&) const = default;
};

struct IrProgram {
    std::vector<IrSymbol> symbols;
    std::vector<IrBody> bodies;
    // Prepared constants that a downstream compilation can inline on use.
    std::vector<IrStaticConstant> static_constants{};
    // C imports still to link; not part of a .nir file.
    std::vector<IrNativeUnit> native_units{};
    // The linked native library, when the program has C imports.
    std::optional<IrNativeLibrary> native_library{};
};

// The source loader can supply this trusted library file and retry lowering.
class MissingPrimitiveWrapper : public Diagnostic {
public:
    MissingPrimitiveWrapper(const Span& span, std::string message, std::string path)
        : Diagnostic(span, std::move(message)), path(std::move(path)) {}

    std::string path;
};

// True when any body of `program` contains a Spawn.
bool has_spawn(const IrProgram& program);
// Marks a poll on every loop and call of a program that contains a Spawn
// (Concurrency.md section 10.2). Does nothing otherwise, and running it again
// changes nothing. Lowering runs it on the linked program, and the build runs
// it again so that a .nir input is treated alike.
void insert_polls(IrProgram& program);

IrProgram lower(const std::vector<ParsedFile>& files);
IrProgram lower(const std::vector<ParsedFile>& files, LinkedImports linked);
IrProgram builtin_class_ir();
IrProgram builtin_reference_ir(const std::string& name, const Span& span);
void print_ir(std::ostream& output, const IrProgram& program);

} // namespace neura
