#include "converter.hpp"

#include "circuit.hpp"
#include "ir-optimization/analysis.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/integer.hpp"
#include "neura-ir/verifier.hpp"
#include "nvr/format.hpp"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace neura::backend {
namespace {

using nvr::absent;
using Role = FunctionOrigin::Role;

// A value during conversion. Immediates and constants never need a block;
// slot operands name committed frame state; bit operands live in the graph
// of the block that computed them.
struct Operand {
    enum class Kind { Void, Immediate, Constant, Slot, Bits };

    Kind kind = Kind::Void;
    std::uint32_t type = 0;
    // Constant or slot index.
    std::uint32_t index = 0;
    // Value of an immediate Boolean or integer.
    std::uint64_t bits = 0;
    Word word;
    // Block generation that computed a Bits operand, and the temporary slot
    // that holds it once that block has closed.
    std::uint64_t generation = 0;
    std::uint32_t home = absent;
};

struct ExitSpec {
    nvr::ExitKind kind = nvr::ExitKind::Continue;
    // Bit that selects this exit. The indicators of a block's exits are
    // mutually exclusive and cover every case in which no check fails.
    Signal when = Signal::of(1);
    // Assignments made only by this exit, after the block's pending ones.
    std::vector<std::pair<std::uint32_t, Operand>> assignments;
    std::uint32_t target = 0;
    nvr::StateOp operation = nvr::StateOp::AllocateObject;
    std::uint32_t immediate = 0;
    std::vector<Operand> arguments;
    std::uint32_t result_slot = absent;
    std::uint32_t resume = 0;
    std::uint32_t error_slot = absent;
    std::uint32_t error_resume = 0;
    std::uint32_t reason = 0;
};

struct OpenBlock {
    std::uint32_t index = 0;
    Circuit circuit;
    std::vector<nvr::Value> inputs;
    std::map<std::pair<nvr::ValueKind, std::uint32_t>, Word> input_lanes;
    std::map<std::vector<WireId>, std::uint32_t> output_lanes;
    // Slot assignments made by the block's statements, committed in
    // parallel by whichever exit is taken.
    std::map<std::uint32_t, Operand> pending;
    // Checks in evaluation order. The first failing check selects a trap.
    std::vector<std::pair<std::uint32_t, Signal>> failures;
    // Candidates for the block's source location, in order of preference:
    // its first check that can fail, the operation that ends it, and its
    // first statement. A runtime fault in a block comes from one of these.
    std::optional<Span> failure_span;
    std::optional<Span> operation_span;
    std::optional<Span> statement_span;
};

// A virtual call or destruction whose targets depend on the classes the
// program instantiates. Its block is built after every reachable function
// has been converted.
struct Dispatch {
    std::uint32_t function = 0;
    std::uint32_t block = 0;
    SymbolId static_class = no_symbol;
    // The method, or no_symbol for destruction.
    SymbolId method = no_symbol;
    std::uint32_t tag_slot = 0;
    Operand receiver;
    std::vector<Operand> arguments;
    std::uint32_t result_slot = absent;
    std::uint32_t resume = 0;
};

constexpr std::uint32_t arithmetic_failure = static_cast<std::uint32_t>(nvr::TrapReason::Arithmetic);
constexpr std::uint32_t bounds_failure = static_cast<std::uint32_t>(nvr::TrapReason::Bounds);
constexpr std::uint32_t program_failure = static_cast<std::uint32_t>(nvr::TrapReason::ProgramFailure);
constexpr std::uint32_t dispatch_failure = static_cast<std::uint32_t>(nvr::TrapReason::InvalidDispatch);

std::vector<std::uint8_t> little_endian(std::uint64_t bits, unsigned bytes) {
    std::vector<std::uint8_t> result;
    for (unsigned i = 0; i < bytes; ++i) {
        result.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    return result;
}

std::uint64_t width_mask(unsigned width) {
    return width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

bool is_comparison(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

class Converter;

class FunctionCompiler {
public:
    FunctionCompiler(Converter& converter, const FunctionOrigin& origin, std::uint32_t index);
    void compile();

private:
    // Blocks.
    std::uint32_t new_block();
    void begin(std::uint32_t index, const Span& span);
    bool open() const { return block_.has_value(); }
    // The open block. Every operation that emits into a block requires one.
    OpenBlock& current();
    void close(std::vector<ExitSpec> exits);
    void jump(std::uint32_t target);
    void branch(const Operand& condition, std::uint32_t when_true, std::uint32_t when_false);
    void finish();

    // Operands.
    std::uint32_t new_slot(std::uint32_t type);
    Operand resolve(const Operand& operand) const;
    Operand slot_operand(std::uint32_t slot) const;
    Operand immediate(std::uint32_t type, std::uint64_t bits) const;
    Operand bits(std::uint32_t type, Word word) const;
    Word lanes(const Operand& operand);
    nvr::Value value(const Operand& operand);
    void spill(Operand& operand);
    void assign(std::uint32_t slot, Operand operand);
    void fail_when(const Signal& condition, std::uint32_t reason, const Span& span);
    void operation_at(const Span& span);

    // Effects that end a block.
    Operand state(nvr::StateOp operation, std::uint32_t immediate, std::vector<Operand> arguments,
                  std::uint32_t result_type, const Span& span);
    Operand invoke(const FunctionOrigin& callee, std::vector<Operand> arguments, std::uint32_t result_type,
                   const Span& span);

    // Statements and expressions.
    void region(const std::vector<IrNode>& statements);
    void statement(const IrNode& node);
    void store(const IrNode& node);
    void branch_statement(const IrNode& node);
    void loop(const IrNode& node);
    void destroy(const IrNode& node);
    Operand expression(const IrNode& node);
    std::vector<Operand> sequence_of(const std::vector<const IrNode*>& nodes);
    std::vector<Operand> sequence(const std::vector<IrNode>& nodes, std::size_t first = 0);
    bool graph_only(const IrNode& node) const;
    Operand integer_literal(const IrNode& node);
    Operand load(const IrNode& node);
    Operand unary(const IrNode& node);
    Operand binary(const IrNode& node);
    Operand isolated_arithmetic(const IrNode& node, Operand left, Operand right, IntegerFormat format);
    Operand short_circuit(const IrNode& node);
    Operand call(const IrNode& node);
    Operand virtual_call(const IrNode& node);
    Operand external_call(const IrNode& node);
    Operand allocate(const IrNode& node);
    Operand to_index(const Operand& index, const IrType& type, const Span& span);
    Operand receiver_value(const IrNode& node);
    SymbolId module_of(const IrNode& receiver) const;
    SymbolId resolve_global(const IrNode& node) const;
    IntegerFormat integer_format(const IrType& type, const Span& span) const;

    // Function bodies.
    void compile_function();
    void compile_constructor();
    void compile_destructor();
    void compile_concatenate();
    void field_initializers(SymbolId cls);
    void self_parameter(SymbolId cls);
    void parameters(SymbolId function);

    Converter& c_;
    FunctionOrigin origin_;
    std::uint32_t index_;
    nvr::Function function_;
    std::vector<std::optional<Span>> spans_;
    std::optional<OpenBlock> block_;
    std::uint64_t generation_ = 0;
    std::map<SymbolId, std::uint32_t> slots_;
    std::uint32_t self_slot_ = absent;
    SymbolId self_class_ = no_symbol;
    SymbolId receiver_module_ = no_symbol;
    struct LoopTargets {
        std::uint32_t head;
        std::uint32_t exit;
    };
    std::vector<LoopTargets> loops_;
    std::vector<std::uint32_t> placeholders_;

    friend class Converter;
};

class Converter {
public:
    Converter(const IrProgram& program, const ConversionOptions& options) : program_(program), options_(options) {}

    Conversion run();

    const IrProgram& program() const { return program_; }
    const IrSymbol& symbol(SymbolId id) const { return program_.symbols.at(id); }

    std::uint32_t type_index(const nvr::Type& type);
    std::uint32_t type_of(const IrType& type, const Span& where);
    std::uint32_t scalar(nvr::TypeKind kind, unsigned width = 0) {
        return type_index({kind, static_cast<std::uint8_t>(width), 0});
    }
    std::uint32_t constant(std::uint32_t type, std::vector<std::uint8_t> payload);
    std::uint32_t text_constant(const std::string& text, const Span& where);
    std::uint32_t layout(SymbolId cls, const Span& where);
    std::uint32_t field_index(SymbolId field, const Span& where);
    std::uint32_t global(SymbolId global, const Span& where);
    std::uint32_t graph(const nvr::Graph& graph);
    std::uint32_t console_import();
    std::uint32_t function(const FunctionOrigin& origin);

    SymbolId member(SymbolId owner, const std::string& name) const;
    SymbolId resolve_member(SymbolId concrete, SymbolId declared) const;
    SymbolId implementation(SymbolId cls, SymbolId method) const;
    bool derives(SymbolId cls, SymbolId base) const;
    bool is_exit_code(SymbolId enumeration) const;
    bool is_println(SymbolId function) const;
    const std::vector<IrNode>& body(SymbolId owner) const;
    const std::vector<const IrNode*>& field_initializers(SymbolId cls) const;
    std::optional<std::uint64_t> literal(const IrNode& node, std::uint32_t type);

    void instantiate(SymbolId cls) { instantiated_.insert(cls); }
    void add_dispatch(Dispatch dispatch) { dispatches_.push_back(std::move(dispatch)); }
    void set_function(std::uint32_t index, nvr::Function function, std::vector<std::optional<Span>> spans,
                      std::vector<std::uint32_t> placeholders);
    const nvr::Function& converted(std::uint32_t index) const { return image_.functions.at(index); }
    const nvr::Type& type_at(std::uint32_t index) const { return image_.types.at(index); }
    const nvr::Global& global_at(std::uint32_t index) const { return image_.globals.at(index); }

private:
    void index_program();
    void check_initializers();
    FunctionOrigin entry_point() const;
    FunctionOrigin target(const Dispatch& dispatch, SymbolId cls) const;
    void build_dispatch(const Dispatch& dispatch);
    void remove_unreachable_blocks();
    void source_map();
    std::string label(const std::string& file) const;

    const IrProgram& program_;
    ConversionOptions options_;
    nvr::Image image_;
    std::map<nvr::Type, std::uint32_t, decltype([](const nvr::Type& a, const nvr::Type& b) {
                 return std::tie(a.kind, a.width, a.reference) < std::tie(b.kind, b.width, b.reference);
             })> types_;
    // Byte strings rather than byte vectors as keys: GCC 14 reports a false
    // -Wstringop-overread for ordered comparisons of std::vector<uint8_t>.
    std::map<std::pair<std::uint32_t, std::string>, std::uint32_t> constants_;
    std::map<SymbolId, std::uint32_t> layouts_;
    std::map<SymbolId, std::uint32_t> fields_;
    std::map<SymbolId, std::uint32_t> globals_;
    std::map<std::string, std::uint32_t> graphs_;
    std::optional<std::uint32_t> console_;
    std::map<FunctionOrigin, std::uint32_t> functions_;
    std::vector<FunctionOrigin> origins_;
    std::size_t next_function_ = 0;
    std::vector<std::vector<std::optional<Span>>> spans_;
    std::vector<std::vector<std::uint32_t>> placeholders_;

    std::map<std::pair<SymbolId, std::string>, SymbolId> members_;
    std::map<SymbolId, const std::vector<IrNode>*> bodies_;
    std::map<SymbolId, std::vector<const IrNode*>> field_initializers_;
    std::map<SymbolId, const IrNode*> global_initializers_;
    std::set<SymbolId> instantiated_;
    std::vector<Dispatch> dispatches_;
};

// ---------------------------------------------------------------------------
// Converter tables

std::uint32_t Converter::type_index(const nvr::Type& type) {
    auto [it, inserted] = types_.emplace(type, static_cast<std::uint32_t>(image_.types.size()));
    if (inserted) {
        image_.types.push_back(type);
    }
    return it->second;
}

bool Converter::is_exit_code(SymbolId enumeration) const {
    const auto& item = symbol(enumeration);
    if (item.kind != SymbolKind::Enum || item.name != "ExitCode" || item.owner == no_symbol) {
        return false;
    }
    const auto& entry = symbol(item.owner);
    return entry.kind == SymbolKind::Module && entry.name == "Entry" && entry.owner != no_symbol &&
           symbol(entry.owner).name == "SysIo" && symbol(entry.owner).owner == no_symbol;
}

bool Converter::is_println(SymbolId function) const {
    const auto& item = symbol(function);
    return item.external && item.name == "println" && item.owner != no_symbol &&
           symbol(item.owner).name == "SysIo" && symbol(item.owner).owner == no_symbol;
}

std::uint32_t Converter::type_of(const IrType& type, const Span& where) {
    switch (type.kind) {
    case TypeKind::Void:
        return scalar(nvr::TypeKind::Void);
    case TypeKind::Bool:
        return scalar(nvr::TypeKind::Bool);
    case TypeKind::Integer: {
        const auto format = specified_integer_format(type);
        if (!format) {
            throw Diagnostic(where, type.name == "byte"
                ? "the signedness of 'byte' is not specified; use uint8 or int8"
                : "the width of '" + type.name + "' is not specified; use a sized integer type such as int32");
        }
        return scalar(format->is_signed ? nvr::TypeKind::Signed : nvr::TypeKind::Unsigned, format->bits);
    }
    case TypeKind::String:
        return scalar(nvr::TypeKind::Text);
    case TypeKind::Float:
        throw Diagnostic(where, "floating-point semantics are not specified; '" + type.name +
                                    "' cannot be converted to neuron graphs");
    case TypeKind::Array:
    case TypeKind::List: {
        const auto element = type_of(type.arguments.front(), where);
        return type_index({type.kind == TypeKind::Array ? nvr::TypeKind::Array : nvr::TypeKind::List, 0, element});
    }
    case TypeKind::Object:
        if (!type.arguments.empty() || !symbol(type.symbol).generics.empty()) {
            throw Diagnostic(where, "generic class '" + symbol(type.symbol).name +
                                        "' is not supported: IRtoNeuronConversion does not specialize generics");
        }
        return type_index({nvr::TypeKind::Object, 0, layout(type.symbol, where)});
    case TypeKind::Enum:
        if (is_exit_code(type.symbol)) {
            return scalar(nvr::TypeKind::Signed, 32);
        }
        throw Diagnostic(where, "enum representation is not specified");
    case TypeKind::TypeParameter:
        throw Diagnostic(where, "type parameter '" + type.name +
                                    "' is not supported: IRtoNeuronConversion does not specialize generics");
    case TypeKind::Module:
    case TypeKind::Callable:
        break;
    }
    throw Diagnostic(where, "'" + type.name + "' is not a value type");
}

std::uint32_t Converter::constant(std::uint32_t type, std::vector<std::uint8_t> payload) {
    auto key = std::make_pair(type, std::string(payload.begin(), payload.end()));
    auto [it, inserted] = constants_.emplace(std::move(key), static_cast<std::uint32_t>(image_.constants.size()));
    if (inserted) {
        image_.constants.push_back({type, std::move(payload)});
    }
    return it->second;
}

std::uint32_t Converter::text_constant(const std::string& text, const Span& where) {
    if (!nvr::valid_utf8(text)) {
        throw Diagnostic(where, "string literal is not valid UTF-8");
    }
    return constant(scalar(nvr::TypeKind::Text), std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::uint32_t Converter::layout(SymbolId cls, const Span& where) {
    if (const auto it = layouts_.find(cls); it != layouts_.end()) {
        return it->second;
    }
    const auto& item = symbol(cls);
    if (!item.generics.empty()) {
        throw Diagnostic(where, "generic class '" + item.name +
                                    "' is not supported: IRtoNeuronConversion does not specialize generics");
    }
    // Reserve the index first: a field may refer to its own class.
    const auto index = static_cast<std::uint32_t>(image_.layouts.size());
    layouts_.emplace(cls, index);
    image_.layouts.emplace_back();
    nvr::Layout result;
    if (item.base != no_symbol) {
        result.parent = layout(item.base, where);
        result.fields = image_.layouts[result.parent].fields;
    }
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& field = program_.symbols[id];
        if (field.owner == cls && field.kind == SymbolKind::Field) {
            fields_[id] = static_cast<std::uint32_t>(result.fields.size());
            result.fields.push_back(type_of(field.type, field.span));
        }
    }
    image_.layouts[index] = std::move(result);
    return index;
}

std::uint32_t Converter::field_index(SymbolId field, const Span& where) {
    layout(symbol(field).owner, where);
    return fields_.at(field);
}

std::optional<std::uint64_t> Converter::literal(const IrNode& node, std::uint32_t type) {
    const auto& t = image_.types[type];
    switch (node.op) {
    case IrOp::Boolean:
        return node.text == "true" ? 1 : 0;
    case IrOp::Integer: {
        const auto value = parse_integer_literal(node.text);
        if (!value) {
            throw Diagnostic(node.span, "malformed integer literal '" + node.text + "'");
        }
        const auto bits = value->negative ? std::uint64_t{0} - value->magnitude : value->magnitude;
        return bits & width_mask(t.width);
    }
    case IrOp::Reference:
        if (node.symbol != no_symbol && symbol(node.symbol).kind == SymbolKind::Variant &&
            is_exit_code(symbol(node.symbol).owner) && symbol(node.symbol).name == "Ok") {
            return 0;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::uint32_t Converter::global(SymbolId id, const Span& where) {
    if (const auto it = globals_.find(id); it != globals_.end()) {
        return it->second;
    }
    const auto& item = symbol(id);
    nvr::Global result;
    result.type = type_of(item.type, item.span);
    const auto init = global_initializers_.find(id);
    if (init == global_initializers_.end()) {
        throw Diagnostic(where, "global '" + item.name + "' has no initializer");
    }
    const auto& node = *init->second;
    if (node.op == IrOp::String) {
        result.initializer = text_constant(node.text, node.span);
    } else {
        const auto bits = literal(node, result.type);
        const auto& t = image_.types[result.type];
        result.initializer = constant(result.type, little_endian(*bits, t.kind == nvr::TypeKind::Bool ? 1 : t.width / 8U));
    }
    const auto index = static_cast<std::uint32_t>(image_.globals.size());
    image_.globals.push_back(result);
    globals_.emplace(id, index);
    return index;
}

std::uint32_t Converter::graph(const nvr::Graph& graph) {
    // Deduplicate by encoding, so that blocks with identical circuits share
    // one graph record.
    std::string key;
    const auto push = [&](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            key.push_back(static_cast<char>(static_cast<std::uint8_t>(value >> (8 * i))));
        }
    };
    push(graph.input_lanes);
    push(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        push(static_cast<std::uint64_t>(node.activation));
        push(static_cast<std::uint64_t>(node.bias));
        push(node.edges.size());
        for (const auto& edge : node.edges) {
            push(edge.source);
            push(static_cast<std::uint64_t>(edge.weight));
        }
    }
    push(graph.outputs.size());
    for (auto output : graph.outputs) {
        push(output);
    }
    auto [it, inserted] = graphs_.emplace(std::move(key), static_cast<std::uint32_t>(image_.graphs.size()));
    if (inserted) {
        image_.graphs.push_back(graph);
    }
    return it->second;
}

std::uint32_t Converter::console_import() {
    if (!console_) {
        nvr::Import import;
        import.name = "neura.console.write_line";
        import.interface_version = 1;
        import.parameter_types = {scalar(nvr::TypeKind::Text)};
        import.success_type = scalar(nvr::TypeKind::Void);
        import.error_type = scalar(nvr::TypeKind::Unsigned, 32);
        console_ = static_cast<std::uint32_t>(image_.imports.size());
        image_.imports.push_back(std::move(import));
    }
    return *console_;
}

std::uint32_t Converter::function(const FunctionOrigin& origin) {
    auto [it, inserted] = functions_.emplace(origin, static_cast<std::uint32_t>(origins_.size()));
    if (inserted) {
        origins_.push_back(origin);
        image_.functions.emplace_back();
        spans_.emplace_back();
        placeholders_.emplace_back();
    }
    return it->second;
}

void Converter::set_function(std::uint32_t index, nvr::Function function, std::vector<std::optional<Span>> spans,
                             std::vector<std::uint32_t> placeholders) {
    image_.functions[index] = std::move(function);
    spans_[index] = std::move(spans);
    placeholders_[index] = std::move(placeholders);
}

SymbolId Converter::member(SymbolId owner, const std::string& name) const {
    const auto it = members_.find({owner, name});
    return it == members_.end() ? no_symbol : it->second;
}

SymbolId Converter::resolve_member(SymbolId concrete, SymbolId declared) const {
    const auto& name = symbol(declared).name;
    for (auto scope = concrete; scope != no_symbol; scope = symbol(scope).base) {
        if (const auto found = member(scope, name); found != no_symbol) {
            return found;
        }
    }
    return declared;
}

SymbolId Converter::implementation(SymbolId cls, SymbolId method) const {
    const auto& name = symbol(method).name;
    for (auto scope = cls; scope != no_symbol; scope = symbol(scope).base) {
        const auto found = member(scope, name);
        if (found != no_symbol && symbol(found).kind == SymbolKind::Function && symbol(found).has_body) {
            return found;
        }
    }
    throw std::logic_error("instantiated class " + symbol(cls).name + " lacks an implementation of " + name);
}

bool Converter::derives(SymbolId cls, SymbolId base) const {
    for (auto scope = cls; scope != no_symbol; scope = symbol(scope).base) {
        if (scope == base) {
            return true;
        }
    }
    return false;
}

const std::vector<IrNode>& Converter::body(SymbolId owner) const {
    const auto it = bodies_.find(owner);
    if (it == bodies_.end()) {
        throw std::logic_error("no body for " + symbol(owner).name);
    }
    return *it->second;
}

const std::vector<const IrNode*>& Converter::field_initializers(SymbolId cls) const {
    static const std::vector<const IrNode*> none;
    const auto it = field_initializers_.find(cls);
    return it == field_initializers_.end() ? none : it->second;
}

// ---------------------------------------------------------------------------
// Program-level checks and the conversion driver

void Converter::index_program() {
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = program_.symbols[id];
        if (item.owner != no_symbol &&
            (item.kind == SymbolKind::Function || item.kind == SymbolKind::Global || item.kind == SymbolKind::Field)) {
            members_.emplace(std::make_pair(item.owner, item.name), id);
        }
    }
    for (const auto& body : program_.bodies) {
        const auto kind = symbol(body.owner).kind;
        if (kind == SymbolKind::Function) {
            bodies_[body.owner] = &body.instructions;
            continue;
        }
        for (const auto& instruction : body.instructions) {
            if (instruction.op != IrOp::Initialize) {
                continue;
            }
            const auto target = symbol(instruction.symbol).kind;
            if (target == SymbolKind::Field) {
                field_initializers_[body.owner].push_back(&instruction);
            } else if (target == SymbolKind::Global) {
                global_initializers_[instruction.symbol] = &instruction.operands.front();
            }
        }
    }
}

void Converter::check_initializers() {
    // Module and file initializers run before the entry function. Their
    // relative order across modules and files is not specified, so only
    // initializers without effects, which order cannot affect, are accepted.
    for (const auto& body : program_.bodies) {
        const auto kind = symbol(body.owner).kind;
        if (kind != SymbolKind::File && kind != SymbolKind::Module) {
            continue;
        }
        for (const auto& instruction : body.instructions) {
            if (instruction.op != IrOp::Initialize) {
                throw Diagnostic(instruction.span,
                                 "top-level statements are not supported: program initialization order is not specified");
            }
            const auto& init = instruction.operands.front();
            const bool constant = init.op == IrOp::Integer || init.op == IrOp::Boolean || init.op == IrOp::String ||
                                  init.op == IrOp::Float ||
                                  (init.op == IrOp::Reference && init.symbol != no_symbol &&
                                   symbol(init.symbol).kind == SymbolKind::Variant);
            if (!constant) {
                throw Diagnostic(init.span, "initializer of '" + symbol(instruction.symbol).name +
                                                "' is not a constant: module initialization order is not specified");
            }
        }
    }
}

FunctionOrigin Converter::entry_point() const {
    SymbolId declared = no_symbol;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = program_.symbols[id];
        if (item.kind == SymbolKind::Function && item.name == "main" && item.owner != no_symbol &&
            symbol(item.owner).name == "Entry" && symbol(item.owner).owner != no_symbol &&
            symbol(symbol(item.owner).owner).owner == no_symbol && symbol(symbol(item.owner).owner).name == "SysIo") {
            declared = id;
        }
    }
    std::vector<SymbolId> candidates;
    if (declared != no_symbol) {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = program_.symbols[id];
            if (item.kind != SymbolKind::Function || !item.has_body) {
                continue;
            }
            for (auto base = item.overrides; base != no_symbol; base = symbol(base).overrides) {
                if (base == declared) {
                    candidates.push_back(id);
                    break;
                }
            }
        }
    }
    Span where;
    for (const auto& item : program_.symbols) {
        if (item.kind == SymbolKind::File) {
            where = item.span;
            break;
        }
    }
    if (candidates.empty()) {
        throw Diagnostic(where, "the program has no entry point: no module overrides SysIo.Entry.main");
    }
    if (candidates.size() > 1) {
        throw Diagnostic(symbol(candidates[1]).span, "the program has more than one entry point");
    }
    const auto main = candidates.front();
    return {Role::Function, main, symbol(main).owner};
}

FunctionOrigin Converter::target(const Dispatch& dispatch, SymbolId cls) const {
    if (dispatch.method == no_symbol) {
        return {Role::Destructor, cls, no_symbol};
    }
    return {Role::Function, implementation(cls, dispatch.method), no_symbol};
}

Conversion Converter::run() {
    index_program();
    check_initializers();
    function(entry_point());
    for (const auto extra : options_.extra_functions) {
        const auto owner = symbol(extra).owner;
        function({Role::Function, extra, symbol(owner).kind == SymbolKind::Module ? owner : no_symbol});
    }
    // Convert until no function or dispatch target is left. Dispatch
    // targets depend on the classes the converted code instantiates.
    for (;;) {
        while (next_function_ < origins_.size()) {
            const auto index = static_cast<std::uint32_t>(next_function_++);
            FunctionCompiler(*this, origins_[index], index).compile();
        }
        for (const auto& dispatch : dispatches_) {
            for (const auto cls : instantiated_) {
                if (derives(cls, dispatch.static_class)) {
                    function(target(dispatch, cls));
                }
            }
        }
        if (next_function_ == origins_.size()) {
            break;
        }
    }
    for (const auto& dispatch : dispatches_) {
        build_dispatch(dispatch);
    }
    remove_unreachable_blocks();
    if (options_.source_map) {
        source_map();
    }
    image_.entry = 0;
    return {std::move(image_), std::move(origins_)};
}

void Converter::build_dispatch(const Dispatch& dispatch) {
    // Compare the receiver's type tag with each instantiated class that can
    // reach this site and call that class's implementation. The receiver
    // crosses the graph as 64 opaque lanes so that each exit can pass it at
    // the parameter type of its target; NVM checks the handle against that
    // type when it decodes the lanes.
    std::vector<std::pair<std::uint32_t, SymbolId>> classes;
    for (const auto cls : instantiated_) {
        if (derives(cls, dispatch.static_class)) {
            classes.emplace_back(layouts_.at(cls), cls);
        }
    }
    std::sort(classes.begin(), classes.end());

    Circuit circuit;
    nvr::Block block;
    std::vector<nvr::Exit> exits;
    std::vector<Signal> indicators;
    if (!classes.empty()) {
        Word tag;
        Word receiver;
        const auto u32 = scalar(nvr::TypeKind::Unsigned, 32);
        block.inputs.push_back({nvr::ValueKind::Slot, u32, dispatch.tag_slot});
        for (int i = 0; i < 32; ++i) {
            tag.push_back(circuit.input());
        }
        block.inputs.push_back({nvr::ValueKind::Slot, dispatch.receiver.type, dispatch.receiver.index});
        for (int i = 0; i < 64; ++i) {
            receiver.push_back(circuit.input());
        }
        std::map<std::uint32_t, std::size_t> exit_of;
        for (const auto& [layout_index, cls] : classes) {
            const auto callee = functions_.at(target(dispatch, cls));
            const auto matches = equal(circuit, tag, constant_word(layout_index, 32));
            auto [it, inserted] = exit_of.emplace(callee, exits.size());
            if (inserted) {
                nvr::Exit exit;
                exit.kind = nvr::ExitKind::Call;
                exit.target = callee;
                exit.result_slot = dispatch.result_slot;
                exit.resume = dispatch.resume;
                exits.push_back(exit);
                indicators.push_back(matches);
            } else {
                indicators[it->second] = indicators[it->second] + matches;
            }
        }
        Signal none = Signal::of(1);
        for (const auto& indicator : indicators) {
            none = none - indicator;
        }
        nvr::Exit trap;
        trap.kind = nvr::ExitKind::Trap;
        trap.reason = dispatch_failure;
        exits.push_back(trap);
        indicators.push_back(none);

        const auto lanes = nvr::selector_lanes(exits.size());
        block.selector_start = circuit.output_count();
        for (unsigned j = 0; j < lanes; ++j) {
            Signal bit;
            for (std::size_t e = 0; e < exits.size(); ++e) {
                if ((e >> j) & 1U) {
                    bit = bit + indicators[e];
                }
            }
            circuit.output(bit);
        }
        const auto receiver_start = circuit.output_count();
        for (const auto& lane : receiver) {
            circuit.output(lane);
        }
        for (auto& exit : exits) {
            if (exit.kind != nvr::ExitKind::Call) {
                continue;
            }
            const auto& callee = converted(exit.target);
            exit.arguments.push_back({nvr::ValueKind::Lanes, callee.frame_types[0], receiver_start});
            for (const auto& argument : dispatch.arguments) {
                if (argument.kind == Operand::Kind::Immediate) {
                    const auto& t = image_.types[argument.type];
                    exit.arguments.push_back({nvr::ValueKind::Constant, argument.type,
                                              constant(argument.type, little_endian(argument.bits,
                                                  t.kind == nvr::TypeKind::Bool ? 1 : t.width / 8U))});
                } else {
                    const auto kind = argument.kind == Operand::Kind::Slot ? nvr::ValueKind::Slot
                                                                           : nvr::ValueKind::Constant;
                    exit.arguments.push_back({kind, argument.type, argument.index});
                }
            }
        }
    } else {
        nvr::Exit trap;
        trap.kind = nvr::ExitKind::Trap;
        trap.reason = dispatch_failure;
        exits.push_back(trap);
    }
    if (exits.size() == 1) {
        block.selector_start = 0;
    }
    block.exits = std::move(exits);
    block.graph = graph(circuit.finish());
    image_.functions[dispatch.function].blocks[dispatch.block] = std::move(block);
}

void Converter::remove_unreachable_blocks() {
    for (std::size_t f = 0; f < image_.functions.size(); ++f) {
        auto& function = image_.functions[f];
        std::vector<std::uint32_t> position(function.blocks.size(), absent);
        std::vector<std::uint32_t> order;
        std::deque<std::uint32_t> queue{0};
        position[0] = 0;
        order.push_back(0);
        while (!queue.empty()) {
            const auto current = queue.front();
            queue.pop_front();
            for (const auto& exit : function.blocks[current].exits) {
                std::vector<std::uint32_t> targets;
                switch (exit.kind) {
                case nvr::ExitKind::Continue:
                case nvr::ExitKind::Call:
                case nvr::ExitKind::State:
                    targets = {exit.resume};
                    break;
                case nvr::ExitKind::Host:
                    targets = {exit.resume, exit.error_resume};
                    break;
                default:
                    break;
                }
                for (const auto next : targets) {
                    if (position[next] == absent) {
                        position[next] = static_cast<std::uint32_t>(order.size());
                        order.push_back(next);
                        queue.push_back(next);
                    }
                }
            }
        }
        // Keep the original relative order of the reachable blocks.
        std::sort(order.begin(), order.end());
        for (std::uint32_t i = 0; i < order.size(); ++i) {
            position[order[i]] = i;
        }
        for (const auto placeholder : placeholders_[f]) {
            if (position[placeholder] != absent && std::binary_search(order.begin(), order.end(), placeholder)) {
                throw std::logic_error("a nonvoid function can finish without returning");
            }
        }
        std::vector<nvr::Block> blocks;
        std::vector<std::optional<Span>> spans;
        for (const auto old : order) {
            auto block = std::move(function.blocks[old]);
            for (auto& exit : block.exits) {
                exit.resume = exit.kind == nvr::ExitKind::Continue || exit.kind == nvr::ExitKind::Call ||
                                      exit.kind == nvr::ExitKind::State || exit.kind == nvr::ExitKind::Host
                                  ? position[exit.resume]
                                  : exit.resume;
                if (exit.kind == nvr::ExitKind::Host) {
                    exit.error_resume = position[exit.error_resume];
                }
            }
            blocks.push_back(std::move(block));
            spans.push_back(spans_[f][old]);
        }
        function.blocks = std::move(blocks);
        spans_[f] = std::move(spans);
    }
}

std::string Converter::label(const std::string& file) const {
    namespace fs = std::filesystem;
    std::optional<fs::path> root;
    for (const auto& item : program_.symbols) {
        if (item.kind != SymbolKind::File) {
            continue;
        }
        const auto directory = fs::path(item.name).parent_path();
        if (!root) {
            root = directory;
            continue;
        }
        fs::path common;
        auto a = root->begin();
        auto b = directory.begin();
        for (; a != root->end() && b != directory.end() && *a == *b; ++a, ++b) {
            common /= *a;
        }
        root = common;
    }
    const auto path = fs::path(file);
    if (!root || root->empty()) {
        return path.filename().generic_string();
    }
    return path.lexically_relative(*root).generic_string();
}

void Converter::source_map() {
    std::vector<nvr::SourceLocation> entries;
    std::map<std::string, std::string> labels;
    const auto clamp = [](std::size_t value) {
        return static_cast<std::uint32_t>(std::min<std::size_t>(value, std::numeric_limits<std::uint32_t>::max()));
    };
    for (std::uint32_t f = 0; f < spans_.size(); ++f) {
        for (std::uint32_t b = 0; b < spans_[f].size(); ++b) {
            const auto& span = spans_[f][b];
            if (!span || span->file.empty()) {
                continue;
            }
            auto [it, inserted] = labels.emplace(span->file, std::string());
            if (inserted) {
                it->second = label(span->file);
            }
            entries.push_back({f, b, it->second, std::max<std::uint32_t>(1, clamp(span->line)),
                               std::max<std::uint32_t>(1, clamp(span->column))});
        }
    }
    image_.source_map = std::move(entries);
}

// ---------------------------------------------------------------------------
// Function compilation: blocks and operands

FunctionCompiler::FunctionCompiler(Converter& converter, const FunctionOrigin& origin, std::uint32_t index)
    : c_(converter), origin_(origin), index_(index) {}

std::uint32_t FunctionCompiler::new_block() {
    function_.blocks.emplace_back();
    spans_.emplace_back();
    return static_cast<std::uint32_t>(function_.blocks.size() - 1);
}

void FunctionCompiler::begin(std::uint32_t index, const Span& span) {
    if (block_) {
        throw std::logic_error("a block is already open");
    }
    block_.emplace();
    current().index = index;
    ++generation_;
    if (!span.file.empty()) {
        spans_[index] = span;
    }
}

OpenBlock& FunctionCompiler::current() {
    if (!block_) {
        throw std::logic_error("no block is open");
    }
    return *block_;
}

std::uint32_t FunctionCompiler::new_slot(std::uint32_t type) {
    function_.frame_types.push_back(type);
    return static_cast<std::uint32_t>(function_.frame_types.size() - 1);
}

Operand FunctionCompiler::resolve(const Operand& operand) const {
    if (operand.kind == Operand::Kind::Bits && operand.generation != generation_) {
        if (operand.home == absent) {
            throw std::logic_error("a graph value was used after its block closed");
        }
        Operand slot;
        slot.kind = Operand::Kind::Slot;
        slot.type = operand.type;
        slot.index = operand.home;
        return slot;
    }
    return operand;
}

Operand FunctionCompiler::slot_operand(std::uint32_t slot) const {
    if (!block_) {
        throw std::logic_error("no block is open");
    }
    if (const auto it = block_->pending.find(slot); it != block_->pending.end()) {
        return it->second;
    }
    Operand result;
    result.kind = Operand::Kind::Slot;
    result.type = function_.frame_types[slot];
    result.index = slot;
    return result;
}

Operand FunctionCompiler::immediate(std::uint32_t type, std::uint64_t value) const {
    Operand result;
    result.kind = Operand::Kind::Immediate;
    result.type = type;
    result.bits = value;
    return result;
}

Operand FunctionCompiler::bits(std::uint32_t type, Word word) const {
    std::uint64_t value = 0;
    if (constant_value(word, value)) {
        return immediate(type, value);
    }
    Operand result;
    result.kind = Operand::Kind::Bits;
    result.type = type;
    result.word = std::move(word);
    result.generation = generation_;
    return result;
}

Word FunctionCompiler::lanes(const Operand& original) {
    const auto operand = resolve(original);
    const auto count = nvr::lane_count(c_.type_at(operand.type));
    switch (operand.kind) {
    case Operand::Kind::Immediate:
        return constant_word(operand.bits, count);
    case Operand::Kind::Bits:
        return operand.word;
    case Operand::Kind::Slot:
    case Operand::Kind::Constant: {
        const auto kind = operand.kind == Operand::Kind::Slot ? nvr::ValueKind::Slot : nvr::ValueKind::Constant;
        const auto key = std::make_pair(kind, operand.index);
        if (const auto it = current().input_lanes.find(key); it != current().input_lanes.end()) {
            return it->second;
        }
        current().inputs.push_back({kind, operand.type, operand.index});
        Word word;
        for (unsigned i = 0; i < count; ++i) {
            word.push_back(current().circuit.input());
        }
        current().input_lanes.emplace(key, word);
        return word;
    }
    case Operand::Kind::Void:
        break;
    }
    throw std::logic_error("a void operand has no lanes");
}

nvr::Value FunctionCompiler::value(const Operand& original) {
    const auto operand = resolve(original);
    switch (operand.kind) {
    case Operand::Kind::Immediate: {
        const auto& t = c_.type_at(operand.type);
        const auto bytes = t.kind == nvr::TypeKind::Bool ? 1U : t.width / 8U;
        return {nvr::ValueKind::Constant, operand.type, c_.constant(operand.type, little_endian(operand.bits, bytes))};
    }
    case Operand::Kind::Constant:
        return {nvr::ValueKind::Constant, operand.type, operand.index};
    case Operand::Kind::Slot:
        return {nvr::ValueKind::Slot, operand.type, operand.index};
    case Operand::Kind::Bits: {
        auto& circuit = current().circuit;
        std::vector<WireId> wires;
        wires.reserve(operand.word.size());
        for (const auto& lane : operand.word) {
            wires.push_back(circuit.materialize(lane));
        }
        auto [it, inserted] = current().output_lanes.emplace(wires, circuit.output_count());
        if (inserted) {
            for (const auto wire : wires) {
                circuit.output(Signal::wire(wire));
            }
        }
        return {nvr::ValueKind::Lanes, operand.type, it->second};
    }
    case Operand::Kind::Void:
        break;
    }
    throw std::logic_error("a void operand has no value");
}

void FunctionCompiler::spill(Operand& operand) {
    if (operand.kind != Operand::Kind::Bits || operand.generation != generation_ || operand.home != absent) {
        return;
    }
    operand.home = new_slot(operand.type);
    current().pending[operand.home] = operand;
}

void FunctionCompiler::assign(std::uint32_t slot, Operand operand) {
    operand = resolve(operand);
    if (operand.kind == Operand::Kind::Void) {
        throw std::logic_error("cannot assign a void value");
    }
    current().pending[slot] = std::move(operand);
}

void FunctionCompiler::operation_at(const Span& span) {
    current().operation_span = span;
}

void FunctionCompiler::fail_when(const Signal& condition, std::uint32_t reason, const Span& span) {
    if (condition.is_constant(0)) {
        return;
    }
    if (!current().failure_span) {
        current().failure_span = span;
    }
    auto& failures = current().failures;
    if (!failures.empty() && failures.back().first == reason) {
        failures.back().second = current().circuit.logical_or(failures.back().second, condition);
    } else {
        failures.emplace_back(reason, condition);
    }
}

void FunctionCompiler::close(std::vector<ExitSpec> exits) {
    auto& open_block = current();
    auto& circuit = open_block.circuit;

    // The first failing check, in evaluation order, selects its trap. The
    // block's own exits apply only when no check fails.
    Signal clear = Signal::of(1);
    std::vector<std::pair<std::uint32_t, Signal>> traps;
    for (const auto& [reason, condition] : open_block.failures) {
        const auto first = circuit.logical_and(condition, clear);
        const auto it = std::find_if(traps.begin(), traps.end(), [&](const auto& trap) { return trap.first == reason; });
        if (it == traps.end()) {
            traps.emplace_back(reason, first);
        } else {
            it->second = it->second + first;
        }
        clear = circuit.logical_and(clear, Circuit::logical_not(condition));
    }
    std::vector<ExitSpec> selected;
    for (auto& exit : exits) {
        exit.when = circuit.logical_and(exit.when, clear);
        if (!exit.when.is_constant(0)) {
            selected.push_back(std::move(exit));
        }
    }
    for (const auto& [reason, when] : traps) {
        if (!when.is_constant(0)) {
            ExitSpec trap;
            trap.kind = nvr::ExitKind::Trap;
            trap.reason = reason;
            trap.when = when;
            selected.push_back(std::move(trap));
        }
    }
    if (selected.empty()) {
        throw std::logic_error("a block has no reachable exit");
    }

    nvr::Block block;
    if (selected.size() > 1) {
        block.selector_start = circuit.output_count();
        const auto lanes = nvr::selector_lanes(selected.size());
        for (unsigned j = 0; j < lanes; ++j) {
            Signal bit;
            for (std::size_t e = 0; e < selected.size(); ++e) {
                if ((e >> j) & 1U) {
                    bit = bit + selected[e].when;
                }
            }
            circuit.output(bit);
        }
    }
    for (const auto& spec : selected) {
        nvr::Exit exit;
        exit.kind = spec.kind;
        if (spec.kind != nvr::ExitKind::Trap && spec.kind != nvr::ExitKind::Return) {
            for (const auto& [slot, operand] : open_block.pending) {
                if (operand.kind == Operand::Kind::Slot && operand.index == slot) {
                    continue;
                }
                exit.assignments.push_back({slot, value(operand)});
            }
            for (const auto& [slot, operand] : spec.assignments) {
                exit.assignments.push_back({slot, value(operand)});
            }
        }
        for (const auto& argument : spec.arguments) {
            exit.arguments.push_back(value(argument));
        }
        exit.target = spec.target;
        exit.operation = spec.operation;
        exit.immediate = spec.immediate;
        exit.result_slot = spec.result_slot;
        exit.resume = spec.resume;
        exit.error_slot = spec.error_slot;
        exit.error_resume = spec.error_resume;
        exit.reason = spec.reason;
        block.exits.push_back(std::move(exit));
    }
    block.inputs = open_block.inputs;
    block.graph = c_.graph(circuit.finish());
    for (const auto* candidate : {&open_block.failure_span, &open_block.operation_span, &open_block.statement_span}) {
        if (*candidate && !(*candidate)->file.empty()) {
            spans_[open_block.index] = **candidate;
            break;
        }
    }
    function_.blocks[open_block.index] = std::move(block);
    block_.reset();
    ++generation_;
}

void FunctionCompiler::jump(std::uint32_t target) {
    ExitSpec exit;
    exit.kind = nvr::ExitKind::Continue;
    exit.resume = target;
    close({exit});
}

void FunctionCompiler::branch(const Operand& condition, std::uint32_t when_true, std::uint32_t when_false) {
    const auto bit = lanes(condition).front();
    ExitSpec yes;
    yes.kind = nvr::ExitKind::Continue;
    yes.when = bit;
    yes.resume = when_true;
    ExitSpec no;
    no.kind = nvr::ExitKind::Continue;
    no.when = Circuit::logical_not(bit);
    no.resume = when_false;
    close({yes, no});
}

void FunctionCompiler::finish() {
    if (open()) {
        ExitSpec exit;
        if (c_.type_at(function_.result_type).kind == nvr::TypeKind::Void) {
            exit.kind = nvr::ExitKind::Return;
        } else {
            // Verified IR returns on every path of a nonvoid function, so
            // this block must be unreachable; removal checks that.
            placeholders_.push_back(current().index);
            exit.kind = nvr::ExitKind::Trap;
            exit.reason = program_failure;
        }
        close({exit});
    }
    c_.set_function(index_, std::move(function_), std::move(spans_), std::move(placeholders_));
}

Operand FunctionCompiler::state(nvr::StateOp operation, std::uint32_t immediate, std::vector<Operand> arguments,
                                std::uint32_t result_type, const Span& span) {
    const auto result_slot = c_.type_at(result_type).kind == nvr::TypeKind::Void ? absent : new_slot(result_type);
    const auto resume = new_block();
    ExitSpec exit;
    exit.kind = nvr::ExitKind::State;
    exit.operation = operation;
    exit.immediate = immediate;
    exit.arguments = std::move(arguments);
    exit.result_slot = result_slot;
    exit.resume = resume;
    operation_at(span);
    close({exit});
    begin(resume, span);
    return result_slot == absent ? Operand{} : slot_operand(result_slot);
}

Operand FunctionCompiler::invoke(const FunctionOrigin& callee, std::vector<Operand> arguments,
                                 std::uint32_t result_type, const Span& span) {
    const auto target = c_.function(callee);
    const auto result_slot = c_.type_at(result_type).kind == nvr::TypeKind::Void ? absent : new_slot(result_type);
    const auto resume = new_block();
    ExitSpec exit;
    exit.kind = nvr::ExitKind::Call;
    exit.target = target;
    exit.arguments = std::move(arguments);
    exit.result_slot = result_slot;
    exit.resume = resume;
    operation_at(span);
    close({exit});
    begin(resume, span);
    return result_slot == absent ? Operand{} : slot_operand(result_slot);
}

// ---------------------------------------------------------------------------
// Statements

void FunctionCompiler::region(const std::vector<IrNode>& statements) {
    for (const auto& item : statements) {
        if (!open()) {
            // Control cannot reach the rest of the region.
            return;
        }
        statement(item);
    }
}

void FunctionCompiler::statement(const IrNode& node) {
    if (!current().statement_span) {
        current().statement_span = node.span;
    }
    switch (node.op) {
    case IrOp::Initialize: {
        if (c_.symbol(node.symbol).kind != SymbolKind::Local) {
            throw std::logic_error("function bodies initialize only locals");
        }
        auto it = slots_.find(node.symbol);
        if (it == slots_.end()) {
            it = slots_.emplace(node.symbol, new_slot(c_.type_of(c_.symbol(node.symbol).type, node.span))).first;
        }
        const auto slot = it->second;
        assign(slot, expression(node.operands.front()));
        return;
    }
    case IrOp::Store:
        store(node);
        return;
    case IrOp::Discard:
        expression(node.operands.front());
        return;
    case IrOp::Block:
        region(node.body);
        return;
    case IrOp::Branch:
        branch_statement(node);
        return;
    case IrOp::Loop:
        loop(node);
        return;
    case IrOp::Return: {
        ExitSpec exit;
        exit.kind = nvr::ExitKind::Return;
        if (!node.operands.empty()) {
            exit.arguments.push_back(expression(node.operands.front()));
        }
        operation_at(node.span);
        close({exit});
        return;
    }
    case IrOp::Break:
        jump(loops_.back().exit);
        return;
    case IrOp::Continue:
        jump(loops_.back().head);
        return;
    case IrOp::Destroy:
        destroy(node);
        return;
    default:
        throw Diagnostic(node.span, "unsupported statement");
    }
}

void FunctionCompiler::store(const IrNode& node) {
    const auto& destination = node.operands[0];
    const auto& source = node.operands[1];
    const auto void_type = c_.scalar(nvr::TypeKind::Void);
    switch (destination.op) {
    case IrOp::Load: {
        const auto kind = c_.symbol(destination.symbol).kind;
        if (kind == SymbolKind::Local || kind == SymbolKind::Parameter) {
            const auto slot = slots_.at(destination.symbol);
            assign(slot, expression(source));
            return;
        }
        const auto global = c_.global(resolve_global(destination), destination.span);
        auto stored = expression(source);
        state(nvr::StateOp::WriteGlobal, global, {stored}, void_type, node.span);
        return;
    }
    case IrOp::Field: {
        auto operands = sequence_of({&destination.operands.front(), &source});
        state(nvr::StateOp::WriteField, c_.field_index(destination.symbol, destination.span), std::move(operands),
              void_type, node.span);
        return;
    }
    case IrOp::Index: {
        auto operands = sequence_of({&destination.operands[0], &destination.operands[1], &source});
        auto index = to_index(operands[1], destination.operands[1].type, destination.span);
        state(nvr::StateOp::WriteElement, 0, {operands[0], index, operands[2]}, void_type, node.span);
        return;
    }
    default:
        throw Diagnostic(destination.span, "unsupported assignment destination");
    }
}

void FunctionCompiler::branch_statement(const IrNode& node) {
    const auto condition = expression(node.operands.front());
    const auto then_block = new_block();
    const auto join = new_block();
    const auto else_block = node.alternative.empty() ? join : new_block();
    branch(condition, then_block, else_block);
    begin(then_block, node.body.empty() ? node.span : node.body.front().span);
    region(node.body);
    if (open()) {
        jump(join);
    }
    if (else_block != join) {
        begin(else_block, node.alternative.front().span);
        region(node.alternative);
        if (open()) {
            jump(join);
        }
    }
    begin(join, node.span);
}

void FunctionCompiler::loop(const IrNode& node) {
    const auto head = new_block();
    jump(head);
    begin(head, node.span);
    const auto condition = expression(node.operands.front());
    const auto body = new_block();
    const auto exit = new_block();
    branch(condition, body, exit);
    loops_.push_back({head, exit});
    begin(body, node.body.empty() ? node.span : node.body.front().span);
    region(node.body);
    if (open()) {
        jump(head);
    }
    loops_.pop_back();
    begin(exit, node.span);
}

void FunctionCompiler::destroy(const IrNode& node) {
    // Run the cleanup chain of the object's runtime class, then free it.
    auto object = expression(node.operands.front());
    c_.type_of(node.operands.front().type, node.span);
    spill(object);
    const auto tag_slot = new_slot(c_.scalar(nvr::TypeKind::Unsigned, 32));
    const auto dispatch_block = new_block();
    const auto resume = new_block();
    ExitSpec read_tag;
    read_tag.kind = nvr::ExitKind::State;
    read_tag.operation = nvr::StateOp::ReadTag;
    read_tag.arguments = {object};
    read_tag.result_slot = tag_slot;
    read_tag.resume = dispatch_block;
    operation_at(node.span);
    close({read_tag});
    object = resolve(object);
    spans_[dispatch_block] = node.span;
    c_.add_dispatch({index_, dispatch_block, node.symbol, no_symbol, tag_slot, object, {}, absent, resume});
    begin(resume, node.span);
    state(nvr::StateOp::FreeObject, 0, {object}, c_.scalar(nvr::TypeKind::Void), node.span);
}

// ---------------------------------------------------------------------------
// Expressions

bool FunctionCompiler::graph_only(const IrNode& node) const {
    switch (node.op) {
    case IrOp::Boolean:
    case IrOp::Integer:
    case IrOp::String:
    case IrOp::Receiver:
    case IrOp::Reference:
        return true;
    case IrOp::Load: {
        const auto kind = c_.symbol(node.symbol).kind;
        return kind == SymbolKind::Local || kind == SymbolKind::Parameter;
    }
    case IrOp::Unary:
        return graph_only(node.operands.front());
    case IrOp::Binary:
        return node.operands.front().type.kind != TypeKind::String &&
               std::all_of(node.operands.begin(), node.operands.end(),
                           [this](const IrNode& operand) { return graph_only(operand); });
    case IrOp::ShortCircuit:
        return graph_only(node.operands.front()) && graph_only(node.body.front()) &&
               optimization::pure_and_total(c_.program(), node.body.front());
    default:
        return false;
    }
}

std::vector<Operand> FunctionCompiler::sequence_of(const std::vector<const IrNode*>& nodes) {
    // Operands are evaluated left to right. A graph value that must outlive
    // a later operand's block is spilled to a temporary slot first.
    std::vector<Operand> result;
    result.reserve(nodes.size());
    for (const auto* node : nodes) {
        if (!graph_only(*node)) {
            for (auto& held : result) {
                spill(held);
            }
        }
        result.push_back(expression(*node));
    }
    for (auto& operand : result) {
        operand = resolve(operand);
    }
    return result;
}

std::vector<Operand> FunctionCompiler::sequence(const std::vector<IrNode>& nodes, std::size_t first) {
    std::vector<const IrNode*> pointers;
    for (std::size_t i = first; i < nodes.size(); ++i) {
        pointers.push_back(&nodes[i]);
    }
    return sequence_of(pointers);
}

IntegerFormat FunctionCompiler::integer_format(const IrType& type, const Span& span) const {
    const auto& t = c_.type_at(c_.type_of(type, span));
    if (t.kind != nvr::TypeKind::Signed && t.kind != nvr::TypeKind::Unsigned) {
        throw Diagnostic(span, "arithmetic requires an integer type");
    }
    return {t.width, t.kind == nvr::TypeKind::Signed};
}

Operand FunctionCompiler::integer_literal(const IrNode& node) {
    const auto type = c_.type_of(node.type, node.span);
    return immediate(type, c_.literal(node, type).value());
}

Operand FunctionCompiler::expression(const IrNode& node) {
    switch (node.op) {
    case IrOp::Boolean:
        return immediate(c_.scalar(nvr::TypeKind::Bool), node.text == "true" ? 1 : 0);
    case IrOp::Integer:
        return integer_literal(node);
    case IrOp::String: {
        Operand result;
        result.kind = Operand::Kind::Constant;
        result.type = c_.scalar(nvr::TypeKind::Text);
        result.index = c_.text_constant(node.text, node.span);
        return result;
    }
    case IrOp::Float:
        c_.type_of(node.type, node.span);
        break;
    case IrOp::Load:
        return load(node);
    case IrOp::Field: {
        auto object = expression(node.operands.front());
        return state(nvr::StateOp::ReadField, c_.field_index(node.symbol, node.span), {object},
                     c_.type_of(node.type, node.span), node.span);
    }
    case IrOp::Reference: {
        const auto type = c_.type_of(node.type, node.span);
        if (const auto value = c_.literal(node, type)) {
            return immediate(type, *value);
        }
        break;
    }
    case IrOp::Receiver:
        return receiver_value(node);
    case IrOp::Unary:
        return unary(node);
    case IrOp::Binary:
        return binary(node);
    case IrOp::ShortCircuit:
        return short_circuit(node);
    case IrOp::Call:
    case IrOp::VirtualCall:
    case IrOp::ExternalCall:
        return call(node);
    case IrOp::Allocate:
        return allocate(node);
    case IrOp::Collection: {
        auto elements = sequence(node.operands);
        const auto type = c_.type_of(node.type, node.span);
        return state(nvr::StateOp::AllocateCollection, type, std::move(elements), type, node.span);
    }
    case IrOp::Index: {
        auto operands = sequence(node.operands);
        auto index = to_index(operands[1], node.operands[1].type, node.span);
        return state(nvr::StateOp::ReadElement, 0, {operands[0], index}, c_.type_of(node.type, node.span), node.span);
    }
    case IrOp::Length:
        throw Diagnostic(node.span, "length has type 'int', whose width is not specified");
    case IrOp::Append: {
        auto operands = sequence(node.operands);
        return state(nvr::StateOp::AppendElement, 0, std::move(operands), c_.scalar(nvr::TypeKind::Void), node.span);
    }
    case IrOp::RemoveAt: {
        auto operands = sequence(node.operands);
        auto index = to_index(operands[1], node.operands[1].type, node.span);
        return state(nvr::StateOp::RemoveElement, 0, {operands[0], index}, c_.scalar(nvr::TypeKind::Void), node.span);
    }
    case IrOp::BaseInitialize: {
        auto operands = sequence(node.operands);
        return invoke({Role::Constructor, c_.symbol(self_class_).base, no_symbol}, std::move(operands),
                      c_.scalar(nvr::TypeKind::Void), node.span);
    }
    default:
        break;
    }
    throw Diagnostic(node.span, "this operation is not supported by IRtoNeuronConversion");
}

Operand FunctionCompiler::load(const IrNode& node) {
    const auto kind = c_.symbol(node.symbol).kind;
    if (kind == SymbolKind::Local || kind == SymbolKind::Parameter) {
        return slot_operand(slots_.at(node.symbol));
    }
    if (kind == SymbolKind::Global) {
        const auto global = c_.global(resolve_global(node), node.span);
        return state(nvr::StateOp::ReadGlobal, global, {}, c_.global_at(global).type, node.span);
    }
    throw Diagnostic(node.span, "unsupported load");
}

Operand FunctionCompiler::receiver_value(const IrNode& node) {
    if (self_slot_ == absent) {
        throw Diagnostic(node.span, "a module is not a value");
    }
    return slot_operand(self_slot_);
}

SymbolId FunctionCompiler::module_of(const IrNode& receiver) const {
    if (receiver.op == IrOp::Reference && receiver.symbol != no_symbol) {
        const auto& item = c_.symbol(receiver.symbol);
        if (item.kind == SymbolKind::Module) {
            return receiver.symbol;
        }
        if (item.kind == SymbolKind::Import) {
            return item.type.symbol;
        }
    }
    if (receiver.op == IrOp::Receiver && c_.symbol(receiver.symbol).kind == SymbolKind::Module &&
        receiver_module_ != no_symbol) {
        return receiver_module_;
    }
    throw Diagnostic(receiver.span, "unsupported module receiver");
}

SymbolId FunctionCompiler::resolve_global(const IrNode& node) const {
    if (node.operands.empty()) {
        return node.symbol;
    }
    return c_.resolve_member(module_of(node.operands.front()), node.symbol);
}

Operand FunctionCompiler::unary(const IrNode& node) {
    const auto operand = expression(node.operands.front());
    if (node.text == "!") {
        return bits(c_.scalar(nvr::TypeKind::Bool), {Circuit::logical_not(lanes(operand).front())});
    }
    const auto format = integer_format(node.type, node.span);
    const auto input = lanes(operand);
    auto result = negate(current().circuit, input, format.is_signed);
    fail_when(result.failure, arithmetic_failure, node.span);
    return bits(c_.type_of(node.type, node.span), std::move(result.value));
}

Operand FunctionCompiler::binary(const IrNode& node) {
    const auto& op = node.text;
    const auto& operand_type = node.operands.front().type;
    if (operand_type.kind == TypeKind::String) {
        auto operands = sequence(node.operands);
        return invoke({Role::Concatenate, no_symbol, no_symbol}, std::move(operands),
                      c_.scalar(nvr::TypeKind::Text), node.span);
    }
    auto operands = sequence(node.operands);
    if ((op == "*" || op == "/") && operands[0].kind != Operand::Kind::Immediate &&
        operands[1].kind != Operand::Kind::Immediate) {
        return isolated_arithmetic(node, operands[0], operands[1], integer_format(operand_type, node.span));
    }
    const auto a = lanes(operands[0]);
    const auto b = lanes(operands[1]);
    auto& circuit = current().circuit;
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    if (operand_type.kind == TypeKind::Bool) {
        const auto same = Circuit::logical_not(circuit.logical_xor(a.front(), b.front()));
        return bits(boolean, {op == "==" ? same : Circuit::logical_not(same)});
    }
    const auto format = integer_format(operand_type, node.span);
    if (is_comparison(op)) {
        Signal result;
        if (op == "==") {
            result = equal(circuit, a, b);
        } else if (op == "!=") {
            result = Circuit::logical_not(equal(circuit, a, b));
        } else if (op == "<") {
            result = less(circuit, a, b, format.is_signed);
        } else if (op == ">") {
            result = less(circuit, b, a, format.is_signed);
        } else if (op == "<=") {
            result = Circuit::logical_not(less(circuit, b, a, format.is_signed));
        } else {
            result = Circuit::logical_not(less(circuit, a, b, format.is_signed));
        }
        return bits(boolean, {result});
    }
    Checked result;
    if (op == "+") {
        result = add(circuit, a, b, format.is_signed);
    } else if (op == "-") {
        result = subtract(circuit, a, b, format.is_signed);
    } else if (op == "*") {
        result = multiply(circuit, a, b, format.is_signed);
    } else if (op == "/") {
        result = divide(circuit, a, b, format.is_signed);
    } else {
        throw Diagnostic(node.span, "operator '" + op + "' is not supported");
    }
    fail_when(result.failure, arithmetic_failure, node.span);
    return bits(c_.type_of(node.type, node.span), std::move(result.value));
}

Operand FunctionCompiler::isolated_arithmetic(const IrNode& node, Operand left, Operand right,
                                              IntegerFormat format) {
    // A multiplier or divider between two variable operands is the largest
    // circuit the converter emits. It gets a block of its own whose graph
    // reads only the two operands, so every site with the same type and
    // operation shares one graph record.
    spill(left);
    spill(right);
    const auto own = new_block();
    jump(own);
    begin(own, node.span);
    const auto a = lanes(left);
    const auto b = lanes(right);
    auto result = node.text == "*" ? multiply(current().circuit, a, b, format.is_signed)
                                   : divide(current().circuit, a, b, format.is_signed);
    fail_when(result.failure, arithmetic_failure, node.span);
    auto value = bits(c_.type_of(node.type, node.span), std::move(result.value));
    spill(value);
    const auto next = new_block();
    jump(next);
    begin(next, node.span);
    return resolve(value);
}

Operand FunctionCompiler::short_circuit(const IrNode& node) {
    const bool conjunction = node.text == "&&";
    const auto& right = node.body.front();
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    if (graph_only(node)) {
        // Evaluating the right operand cannot fail or have an effect, so the
        // graph may compute both operands.
        const auto a = lanes(expression(node.operands.front())).front();
        const auto b = lanes(expression(right)).front();
        auto& circuit = current().circuit;
        return bits(boolean, {conjunction ? circuit.logical_and(a, b) : circuit.logical_or(a, b)});
    }
    const auto left = expression(node.operands.front());
    const auto result = new_slot(boolean);
    const auto evaluate_right = new_block();
    const auto join = new_block();
    const auto bit = lanes(left).front();
    // The left operand alone decides the result when it is false for &&
    // or true for ||.
    ExitSpec decided;
    decided.kind = nvr::ExitKind::Continue;
    decided.when = conjunction ? Circuit::logical_not(bit) : bit;
    decided.resume = join;
    decided.assignments.emplace_back(result, immediate(boolean, conjunction ? 0 : 1));
    ExitSpec undecided;
    undecided.kind = nvr::ExitKind::Continue;
    undecided.when = Circuit::logical_not(decided.when);
    undecided.resume = evaluate_right;
    close({undecided, decided});
    begin(evaluate_right, right.span);
    auto value = expression(right);
    ExitSpec done;
    done.kind = nvr::ExitKind::Continue;
    done.resume = join;
    done.assignments.emplace_back(result, value);
    close({done});
    begin(join, node.span);
    return slot_operand(result);
}

Operand FunctionCompiler::call(const IrNode& node) {
    if (node.op == IrOp::ExternalCall) {
        return external_call(node);
    }
    const auto& target = c_.symbol(node.symbol);
    if (!node.type_arguments.empty() || !target.generics.empty()) {
        throw Diagnostic(node.span, "generic function '" + target.name +
                                        "' is not supported: IRtoNeuronConversion does not specialize generics");
    }
    if (node.op == IrOp::VirtualCall) {
        return virtual_call(node);
    }
    const auto owner = target.owner;
    if (owner != no_symbol && c_.symbol(owner).kind == SymbolKind::Class) {
        throw Diagnostic(node.span, "a method call requires a receiver");
    }
    const auto receiver = owner != no_symbol && c_.symbol(owner).kind == SymbolKind::Module ? owner : no_symbol;
    auto arguments = sequence(node.operands);
    return invoke({Role::Function, node.symbol, receiver}, std::move(arguments), c_.type_of(node.type, node.span),
                  node.span);
}

Operand FunctionCompiler::virtual_call(const IrNode& node) {
    const auto& receiver = node.operands.front();
    const auto result_type = c_.type_of(node.type, node.span);
    if (receiver.type.kind == TypeKind::Module) {
        // Module receivers are known statically: specialize the most derived
        // implementation for the concrete module.
        const auto module = module_of(receiver);
        const auto implementation = c_.resolve_member(module, node.symbol);
        if (!c_.symbol(implementation).has_body) {
            throw Diagnostic(node.span, "module '" + c_.symbol(module).name + "' does not implement '" +
                                            c_.symbol(node.symbol).name + "'");
        }
        auto arguments = sequence(node.operands, 1);
        return invoke({Role::Function, implementation, module}, std::move(arguments), result_type, node.span);
    }
    c_.type_of(receiver.type, receiver.span);
    if (receiver.type.kind != TypeKind::Object) {
        throw Diagnostic(receiver.span, "unsupported method receiver");
    }
    auto operands = sequence(node.operands);
    for (auto& operand : operands) {
        spill(operand);
    }
    const auto tag_slot = new_slot(c_.scalar(nvr::TypeKind::Unsigned, 32));
    const auto dispatch_block = new_block();
    const auto resume = new_block();
    const auto result_slot = c_.type_at(result_type).kind == nvr::TypeKind::Void ? absent : new_slot(result_type);
    ExitSpec read_tag;
    read_tag.kind = nvr::ExitKind::State;
    read_tag.operation = nvr::StateOp::ReadTag;
    read_tag.arguments = {operands.front()};
    read_tag.result_slot = tag_slot;
    read_tag.resume = dispatch_block;
    operation_at(node.span);
    close({read_tag});
    for (auto& operand : operands) {
        operand = resolve(operand);
    }
    spans_[dispatch_block] = node.span;
    Dispatch dispatch{index_, dispatch_block, receiver.type.symbol, node.symbol, tag_slot, operands.front(),
                      std::vector<Operand>(operands.begin() + 1, operands.end()), result_slot, resume};
    c_.add_dispatch(std::move(dispatch));
    begin(resume, node.span);
    return result_slot == absent ? Operand{} : slot_operand(result_slot);
}

Operand FunctionCompiler::external_call(const IrNode& node) {
    if (!c_.is_println(node.symbol)) {
        const auto& target = c_.symbol(node.symbol);
        throw Diagnostic(node.span, "external function '" + c_.symbol(target.owner).name + "." + target.name +
                                        "' has no runtime binding");
    }
    // SysIo.println writes through neura.console.write_line. A failed write
    // ends the run: the language has no error result for println yet.
    auto arguments = sequence(node.operands);
    const auto error_slot = new_slot(c_.scalar(nvr::TypeKind::Unsigned, 32));
    const auto resume = new_block();
    const auto failed = new_block();
    ExitSpec host;
    host.kind = nvr::ExitKind::Host;
    host.target = c_.console_import();
    host.arguments = std::move(arguments);
    host.resume = resume;
    host.error_slot = error_slot;
    host.error_resume = failed;
    operation_at(node.span);
    close({host});
    begin(failed, node.span);
    ExitSpec trap;
    trap.kind = nvr::ExitKind::Trap;
    trap.reason = program_failure;
    close({trap});
    begin(resume, node.span);
    return {};
}

Operand FunctionCompiler::allocate(const IrNode& node) {
    const auto cls = node.type.symbol;
    const auto type = c_.type_of(node.type, node.span);
    auto arguments = sequence(node.operands);
    for (auto& argument : arguments) {
        spill(argument);
    }
    c_.instantiate(cls);
    auto handle = state(nvr::StateOp::AllocateObject, c_.layout(cls, node.span), {}, type, node.span);
    std::vector<Operand> constructor{handle};
    for (const auto& argument : arguments) {
        constructor.push_back(resolve(argument));
    }
    invoke({Role::Constructor, cls, no_symbol}, std::move(constructor), c_.scalar(nvr::TypeKind::Void), node.span);
    return handle;
}

Operand FunctionCompiler::to_index(const Operand& index, const IrType& type, const Span& span) {
    // State operations take unsigned 64-bit indices. A negative signed index
    // is a source bounds failure and is checked before the conversion.
    const auto format = integer_format(type, span);
    auto word = lanes(index);
    if (format.is_signed) {
        fail_when(word.back(), bounds_failure, span);
    }
    word.resize(64, Signal::of(0));
    return bits(c_.scalar(nvr::TypeKind::Unsigned, 64), std::move(word));
}

// ---------------------------------------------------------------------------
// Function bodies

void FunctionCompiler::self_parameter(SymbolId cls) {
    self_class_ = cls;
    self_slot_ = new_slot(c_.type_of(c_.symbol(cls).type, c_.symbol(cls).span));
}

void FunctionCompiler::parameters(SymbolId function) {
    for (const auto parameter : c_.symbol(function).parameters) {
        slots_[parameter] = new_slot(c_.type_of(c_.symbol(parameter).type, c_.symbol(parameter).span));
    }
}

void FunctionCompiler::field_initializers(SymbolId cls) {
    const auto void_type = c_.scalar(nvr::TypeKind::Void);
    for (const auto* initializer : c_.field_initializers(cls)) {
        auto value = expression(initializer->operands.front());
        state(nvr::StateOp::WriteField, c_.field_index(initializer->symbol, initializer->span),
              {slot_operand(self_slot_), value}, void_type, initializer->span);
    }
}

void FunctionCompiler::compile_function() {
    const auto& function = c_.symbol(origin_.symbol);
    if (!function.generics.empty()) {
        throw Diagnostic(function.span, "generic function '" + function.name +
                                            "' is not supported: IRtoNeuronConversion does not specialize generics");
    }
    const auto owner = function.owner;
    if (owner != no_symbol && c_.symbol(owner).kind == SymbolKind::Class) {
        self_parameter(owner);
    } else if (owner != no_symbol && c_.symbol(owner).kind == SymbolKind::Module) {
        receiver_module_ = origin_.receiver;
    }
    parameters(origin_.symbol);
    function_.parameter_count = static_cast<std::uint32_t>(function_.frame_types.size());
    function_.result_type = c_.type_of(function.type, function.span);
    begin(new_block(), function.span);
    region(c_.body(origin_.symbol));
    finish();
}

void FunctionCompiler::compile_constructor() {
    // Base initialization, then this class's field initializers, then the
    // rest of its declared constructor (Object lifecycle, Construction).
    const auto cls = origin_.symbol;
    const auto& item = c_.symbol(cls);
    self_parameter(cls);
    const auto init = c_.member(cls, "init");
    if (init != no_symbol) {
        parameters(init);
    }
    function_.parameter_count = static_cast<std::uint32_t>(function_.frame_types.size());
    function_.result_type = c_.scalar(nvr::TypeKind::Void);
    begin(new_block(), item.span);
    if (init != no_symbol) {
        const auto& body = c_.body(init);
        std::size_t start = 0;
        if (item.base != no_symbol && !body.empty()) {
            // A derived constructor starts with super.init.
            statement(body.front());
            start = 1;
        }
        if (open()) {
            field_initializers(cls);
        }
        for (std::size_t i = start; i < body.size() && open(); ++i) {
            statement(body[i]);
        }
    } else {
        if (item.base != no_symbol) {
            invoke({Role::Constructor, item.base, no_symbol}, {slot_operand(self_slot_)},
                   c_.scalar(nvr::TypeKind::Void), item.span);
        }
        field_initializers(cls);
    }
    finish();
}

void FunctionCompiler::compile_destructor() {
    // Cleanup runs from the most derived class toward its bases.
    const auto cls = origin_.symbol;
    const auto& item = c_.symbol(cls);
    self_parameter(cls);
    function_.parameter_count = 1;
    function_.result_type = c_.scalar(nvr::TypeKind::Void);
    begin(new_block(), item.span);
    const auto void_type = c_.scalar(nvr::TypeKind::Void);
    const auto cleanup = c_.member(cls, "deinit");
    if (cleanup != no_symbol && c_.symbol(cleanup).kind == SymbolKind::Function && c_.symbol(cleanup).has_body) {
        invoke({Role::Function, cleanup, no_symbol}, {slot_operand(self_slot_)}, void_type, item.span);
    }
    if (item.base != no_symbol) {
        invoke({Role::Destructor, item.base, no_symbol}, {slot_operand(self_slot_)}, void_type, item.span);
    }
    finish();
}

void FunctionCompiler::compile_concatenate() {
    // Copies the UTF-8 bytes of both operands into a uint8 list and builds
    // text from it. Concatenating valid UTF-8 yields valid UTF-8.
    const auto text = c_.scalar(nvr::TypeKind::Text);
    const auto u8 = c_.scalar(nvr::TypeKind::Unsigned, 8);
    const auto u64 = c_.scalar(nvr::TypeKind::Unsigned, 64);
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    const auto void_type = c_.scalar(nvr::TypeKind::Void);
    const auto list = c_.type_index({nvr::TypeKind::List, 0, u8});
    const std::uint32_t sources[] = {new_slot(text), new_slot(text)};
    function_.parameter_count = 2;
    function_.result_type = text;
    const Span none;
    begin(new_block(), none);
    const auto buffer = state(nvr::StateOp::AllocateCollection, list, {}, list, none);
    for (const auto source : sources) {
        const auto length = state(nvr::StateOp::ReadLength, 0, {slot_operand(source)}, u64, none);
        const auto position = new_slot(u64);
        assign(position, immediate(u64, 0));
        const auto head = new_block();
        jump(head);
        begin(head, none);
        const auto more = less(current().circuit, lanes(slot_operand(position)), lanes(length), false);
        const auto body = new_block();
        const auto next = new_block();
        branch(bits(boolean, {more}), body, next);
        begin(body, none);
        const auto byte = state(nvr::StateOp::ReadElement, 0, {slot_operand(source), slot_operand(position)}, u8, none);
        // position < length <= 2^64 - 1, so the increment cannot wrap.
        auto incremented = add(current().circuit, lanes(slot_operand(position)), constant_word(1, 64), false);
        assign(position, bits(u64, std::move(incremented.value)));
        state(nvr::StateOp::AppendElement, 0, {buffer, byte}, void_type, none);
        jump(head);
        begin(next, none);
    }
    const auto result = state(nvr::StateOp::ConstructText, text, {buffer}, text, none);
    ExitSpec exit;
    exit.kind = nvr::ExitKind::Return;
    exit.arguments = {result};
    close({exit});
    finish();
}

void FunctionCompiler::compile() {
    switch (origin_.role) {
    case Role::Function: compile_function(); break;
    case Role::Constructor: compile_constructor(); break;
    case Role::Destructor: compile_destructor(); break;
    case Role::Concatenate: compile_concatenate(); break;
    }
}

} // namespace

Conversion convert(const IrProgram& program, const ConversionOptions& options) {
    return Converter(program, options).run();
}

Conversion build(IrProgram program, const BuildOptions& options) {
    if (options.optimize) {
        optimize(program);
    } else {
        require_valid_ir(program, "before conversion");
    }
    ConversionOptions conversion;
    conversion.source_map = options.source_map;
    return convert(program, conversion);
}

} // namespace neura::backend
