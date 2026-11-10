#include "converter.hpp"

#include "block_synth.hpp"
#include "circuit.hpp"
#include "float_circuit.hpp"
#include "ir-optimization/analysis.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/aggregate.hpp"
#include "neura-ir/float.hpp"
#include "neura-ir/host_binding.hpp"
#include "neura-ir/integer.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "nvr/format.hpp"
#include "nvr/names.hpp"

#include <algorithm>
#include <array>
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

struct OpenBlock {
    explicit OpenBlock(BlockTables& tables) : synth(tables) {}

    std::uint32_t index = 0;
    BlockSynthesizer synth;
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
constexpr std::uint32_t iteration_failure = static_cast<std::uint32_t>(nvr::TrapReason::ModifiedDuringIteration);

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
    static bool may_isolate(const IrNode& node);
    Operand integer_literal(const IrNode& node);
    Operand load(const IrNode& node);
    Operand unary(const IrNode& node);
    Operand binary(const IrNode& node);
    Operand isolated_arithmetic(const IrNode& node, Operand left, Operand right, IntegerFormat format);
    Operand short_circuit(const IrNode& node);
    Operand scalar_member(IrOp op, const IrType& type, std::vector<Operand> operands, const Span& span);
    Operand convert(const IrNode& node);
    Operand float_binary(const IrNode& node, Operand left, Operand right);
    FloatFormat float_of(const IrType& type) const;
    Operand call(const IrNode& node);
    Operand virtual_call(const IrNode& node);
    Operand dispatch_call(SymbolId cls, SymbolId method, std::vector<Operand> operands,
                          std::uint32_t result_type, const Span& span);
    Operand value_member(IrOp op, const IrType& type, std::vector<Operand> operands, const Span& span);
    Operand external_call(const IrNode& node);
    Operand bound_host_call(const IrNode& node);
    Operand allocate(const IrNode& node);
    Operand aggregate(const IrNode& node);
    Operand aggregate_value(const IrType& type, SymbolId variant, std::vector<Operand> fields,
                            std::string_view order, const Span& span);
    Operand select_value(const IrNode& node);
    Operand to_index(const Operand& index, const IrType& type, const Span& span);
    Operand receiver_value(const IrNode& node);
    SymbolId module_of(const IrNode& receiver) const;
    SymbolId resolve_global(const IrNode& node);
    IntegerFormat integer_format(const IrType& type, const Span& span) const;

    // Function bodies.
    void compile_function();
    void compile_constructor();
    void compile_destructor();
    void compile_concatenate();
    void compile_hash_text();
    void compile_value_member(IrOp op);
    void append_text_bytes(const Operand& buffer, const Operand& text, const Span& span);
    void compile_start();
    void return_standard_status(const Operand& value, const std::array<SymbolId, 3>& variants, const Span& span);
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
        // Where `continue` goes: the latch, or the head when there is none.
        std::uint32_t next;
        std::uint32_t exit;
    };
    std::vector<LoopTargets> loops_;
    std::vector<std::uint32_t> placeholders_;

    friend class Converter;
};

class Converter : public BlockTables {
public:
    Converter(const IrProgram& program, const ConversionOptions& options) : program_(program), options_(options) {}

    Conversion run();

    const IrProgram& program() const { return program_; }
    const IrSymbol& symbol(SymbolId id) const { return program_.symbols.at(id); }
    std::string qualified(SymbolId id) const;

    std::uint32_t type_index(const nvr::Type& type);
    std::uint32_t type_of(const IrType& type, const Span& where);
    std::uint32_t scalar(nvr::TypeKind kind, unsigned width = 0) {
        return type_index({kind, static_cast<std::uint8_t>(width), 0});
    }
    const nvr::Type& type(std::uint32_t index) const override { return image_.types.at(index); }
    std::uint32_t constant(std::uint32_t type, std::vector<std::uint8_t> payload) override;
    std::uint32_t text_constant(const std::string& text, const Span& where);
    std::uint32_t layout(SymbolId cls, const Span& where);
    std::uint32_t tuple_layout(const IrType& type, const Span& where);
    std::uint32_t payload_start(SymbolId variant, const Span& where);
    std::uint32_t field_index(SymbolId field, const Span& where);
    std::uint32_t global(SymbolId global, const Span& where);
    std::uint32_t graph(const nvr::Graph& graph) override;
    // The import that an external function is bound to, or nothing when the
    // function has no runtime binding.
    std::optional<std::uint32_t> external_import(SymbolId function, const Span& where);
    std::uint32_t function(const FunctionOrigin& origin);
    FunctionOrigin value_function(IrOp op, const IrType& type, const Span& span);
    std::pair<IrType, Span> value_type(SymbolId id) const { return value_types_.at(id); }

    SymbolId member(SymbolId owner, const std::string& name) const;
    SymbolId resolve_member(SymbolId concrete, SymbolId declared) const;
    SymbolId implementation(SymbolId cls, SymbolId method) const;
    bool derives(SymbolId cls, SymbolId base) const;
    std::optional<std::array<SymbolId, 3>> standard_exit_variants(SymbolId enumeration) const;
    const nvr::Import& import_at(std::uint32_t index) const { return image_.imports.at(index); }
    const std::vector<IrNode>& body(SymbolId owner) const;
    const std::vector<const IrNode*>& field_initializers(SymbolId cls) const;
    std::optional<std::uint64_t> literal(const IrNode& node, std::uint32_t type);
    // True for a global initializer that becomes an image constant.
    bool constant_initializer(const IrNode& node) const;
    struct Startup {
        const IrNode* node;
        // The module whose members the node refers to through receivers.
        SymbolId module;
    };
    const std::vector<Startup>& startup() const { return startup_; }
    // Position of a variant among its enum's variants. ExitCode.Ok is 0.
    std::uint64_t ordinal(SymbolId variant) const;

    void instantiate(SymbolId cls) { instantiated_.insert(cls); }
    void add_dispatch(Dispatch dispatch) { dispatches_.push_back(std::move(dispatch)); }
    void set_function(std::uint32_t index, nvr::Function function, std::vector<std::optional<Span>> spans,
                      std::vector<std::uint32_t> placeholders);
    void name_slots(std::uint32_t index, std::vector<std::string> names) { slot_names_[index] = std::move(names); }
    const nvr::Function& converted(std::uint32_t index) const { return image_.functions.at(index); }
    const nvr::Type& type_at(std::uint32_t index) const { return image_.types.at(index); }
    const nvr::Global& global_at(std::uint32_t index) const { return image_.globals.at(index); }

private:
    void index_program();
    void order_initialization();
    SymbolId file_of(SymbolId id) const;
    FunctionOrigin entry_point() const;
    FunctionOrigin target(const Dispatch& dispatch, SymbolId cls) const;
    void build_dispatch(const Dispatch& dispatch);
    void thread_jumps();
    void remove_unreachable_blocks();
    void source_map();
    void names();
    std::string label(const std::string& file) const;

    const IrProgram& program_;
    ConversionOptions options_;
    std::vector<std::pair<IrType, Span>> value_types_;
    nvr::Image image_;
    std::map<nvr::Type, std::uint32_t, decltype([](const nvr::Type& a, const nvr::Type& b) {
                 return std::tie(a.kind, a.width, a.reference) < std::tie(b.kind, b.width, b.reference);
             })> types_;
    // Byte strings rather than byte vectors as keys: GCC 14 reports a false
    // -Wstringop-overread for ordered comparisons of std::vector<uint8_t>.
    std::map<std::pair<std::uint32_t, std::string>, std::uint32_t> constants_;
    std::map<SymbolId, std::uint32_t> layouts_;
    std::vector<std::pair<IrType, std::uint32_t>> tuple_layouts_;
    std::map<SymbolId, std::uint32_t> payload_starts_;
    std::map<SymbolId, std::uint32_t> fields_;
    std::map<SymbolId, std::uint32_t> globals_;
    std::map<std::string, std::uint32_t> graphs_;
    std::map<std::pair<std::string, std::uint32_t>, std::uint32_t> imports_;
    std::map<FunctionOrigin, std::uint32_t> functions_;
    std::vector<FunctionOrigin> origins_;
    std::size_t next_function_ = 0;
    std::vector<std::vector<std::optional<Span>>> spans_;
    std::vector<std::vector<std::uint32_t>> placeholders_;
    std::map<std::uint32_t, std::vector<std::string>> slot_names_;

    std::map<std::pair<SymbolId, std::string>, SymbolId> members_;
    std::map<SymbolId, const std::vector<IrNode>*> bodies_;
    std::map<SymbolId, std::vector<const IrNode*>> field_initializers_;
    std::map<SymbolId, const IrNode*> global_initializers_;
    std::vector<Startup> startup_;
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

std::optional<std::array<SymbolId, 3>> Converter::standard_exit_variants(SymbolId enumeration) const {
    if (enumeration == no_symbol) return std::nullopt;
    const auto& item = symbol(enumeration);
    if (item.kind != SymbolKind::Enum || item.name != "ExitCode" || item.owner == no_symbol) return std::nullopt;
    const auto& program = symbol(item.owner);
    if (program.kind != SymbolKind::Module || program.name != "Program" || program.owner == no_symbol ||
        symbol(program.owner).library_path != "std/program.nra") return std::nullopt;
    const std::array<std::string_view, 3> names{"Ok", "Failure", "Status"};
    std::array variants{no_symbol, no_symbol, no_symbol};
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& value = symbol(id);
        if (value.kind != SymbolKind::Variant || value.owner != enumeration) continue;
        const auto position = std::ranges::find(names, value.name);
        if (position == names.end()) return std::nullopt;
        auto& variant = variants[static_cast<std::size_t>(position - names.begin())];
        if (variant != no_symbol) return std::nullopt;
        variant = id;
    }
    if (std::ranges::find(variants, no_symbol) != variants.end() ||
        !symbol(variants[0]).payload_types.empty() || !symbol(variants[1]).payload_types.empty() ||
        symbol(variants[2]).payload_types != std::vector{IrType{TypeKind::Integer, "int32", no_symbol, {}}})
        return std::nullopt;
    return variants;
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
    case TypeKind::Float: {
        const auto format = float_format(type);
        if (!format) {
            throw Diagnostic(where, "'" + type.name + "' is not a floating-point format");
        }
        return scalar(*format == bfloat16 ? nvr::TypeKind::BFloat : nvr::TypeKind::Float, format->width());
    }
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
    case TypeKind::Tuple:
        return type_index({nvr::TypeKind::Object, 0, tuple_layout(type, where)});
    case TypeKind::Enum:
        // A fieldless enum is its variant ordinal (Types.md, Enums).
        if (has_enum_payload(program_, type.symbol))
            return type_index({nvr::TypeKind::Object, 0, layout(type.symbol, where)});
        return scalar(nvr::TypeKind::Unsigned, 32);
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
    if (item.kind == SymbolKind::Enum) {
        result.fields.push_back(scalar(nvr::TypeKind::Unsigned, 32));
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& variant = symbol(id);
            if (variant.owner != cls || variant.kind != SymbolKind::Variant) continue;
            payload_starts_[id] = static_cast<std::uint32_t>(result.fields.size());
            for (const auto& field : variant.payload_types)
                result.fields.push_back(type_of(field, variant.span));
        }
        image_.layouts[index] = std::move(result);
        return index;
    }
    if (item.base != no_symbol) {
        result.parent = layout(item.base, where);
        result.fields = image_.layouts[result.parent].fields;
    }
    for (const auto& interface : item.interfaces)
        result.interfaces.push_back(layout(interface.symbol, where));
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

std::uint32_t Converter::tuple_layout(const IrType& type, const Span& where) {
    const auto existing = std::ranges::find(tuple_layouts_, type,
                                            &std::pair<IrType, std::uint32_t>::first);
    if (existing != tuple_layouts_.end()) return existing->second;
    const auto index = static_cast<std::uint32_t>(image_.layouts.size());
    tuple_layouts_.emplace_back(type, index);
    image_.layouts.emplace_back();
    nvr::Layout result;
    for (const auto& field : type.arguments) result.fields.push_back(type_of(field, where));
    image_.layouts[index] = std::move(result);
    return index;
}

std::uint32_t Converter::payload_start(SymbolId variant, const Span& where) {
    layout(symbol(variant).owner, where);
    return payload_starts_.at(variant);
}

std::uint32_t Converter::field_index(SymbolId field, const Span& where) {
    layout(symbol(field).owner, where);
    return fields_.at(field);
}

std::uint64_t Converter::ordinal(SymbolId variant) const {
    return enum_ordinal(program_, variant);
}

std::optional<std::uint64_t> Converter::literal(const IrNode& node, std::uint32_t type) {
    const auto& t = image_.types[type];
    switch (node.op) {
    case IrOp::Nil:
        return 0;
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
        if (node.symbol != no_symbol && symbol(node.symbol).kind == SymbolKind::Variant) {
            return ordinal(node.symbol);
        }
        return std::nullopt;
    case IrOp::Float: {
        const auto parsed = parse_float_literal(node.text, float_format(node.type).value());
        if (parsed.error) {
            throw Diagnostic(node.span, "float literal '" + node.text + "' does not fit " + node.type.name);
        }
        return parsed.bits;
    }
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
    } else if (constant_initializer(node)) {
        const auto bits = literal(node, result.type);
        const auto& t = image_.types[result.type];
        result.initializer = constant(result.type, immediate_payload(t, bits.value()));
    }
    // Any other global starts uninitialized; the start function assigns it,
    // and a read before then is an NVM fault.
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

std::optional<std::uint32_t> Converter::external_import(SymbolId function, const Span& where) {
    // A standard library host binding and the service it names. It takes its
    // declared parameters, succeeds with the Ok payload or no value, and fails
    // with a uint32 code (docs/runtime/NVM.md, Host calls).
    const auto& item = symbol(function);
    if (!item.host_binding) return std::nullopt;
    nvr::Import import;
    import.name = item.host_binding->name;
    import.interface_version = item.host_binding->version;
    for (const auto parameter : item.parameters) {
        import.parameter_types.push_back(type_of(symbol(parameter).type, where));
    }
    const auto variants = host_variants(program_, item.type).value();
    const auto& payload = symbol(variants.success).payload_types;
    import.success_type = payload.empty() ? scalar(nvr::TypeKind::Void) : type_of(payload.front(), where);
    import.error_type = scalar(nvr::TypeKind::Unsigned, 32);
    const auto key = std::pair{import.name, import.interface_version};
    if (const auto found = imports_.find(key); found != imports_.end()) {
        if (image_.imports[found->second] != import)
            throw Diagnostic(where, "conflicting signatures for host service '" + import.name + "'");
        return found->second;
    }
    const auto index = static_cast<std::uint32_t>(image_.imports.size());
    image_.imports.push_back(std::move(import));
    imports_.emplace(key, index);
    return index;
}

FunctionOrigin Converter::value_function(IrOp op, const IrType& type, const Span& span) {
    using Entry = std::pair<IrType, Span>;
    const auto found = std::ranges::find(value_types_, type, &Entry::first);
    const auto id = static_cast<SymbolId>(found - value_types_.begin());
    if (found == value_types_.end()) value_types_.emplace_back(type, span);
    const auto role = op == IrOp::ValueEquals ? Role::ValueEquals :
        op == IrOp::ValueHash ? Role::ValueHash : Role::ValueText;
    return {role, id, no_symbol};
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
    std::vector<SymbolId> pending{cls};
    std::set<SymbolId> seen;
    std::vector<SymbolId> candidates;
    while (!pending.empty()) {
        const auto scope = pending.back();
        pending.pop_back();
        if (!seen.insert(scope).second) continue;
        const auto& type = symbol(scope);
        if (type.kind == SymbolKind::Interface) {
            if (const auto found = member(scope, name); found != no_symbol)
                candidates.push_back(found);
        }
        if (type.base != no_symbol) pending.push_back(type.base);
        for (const auto& interface : type.interfaces) pending.push_back(interface.symbol);
    }
    for (const auto candidate : candidates) {
        const auto& function = symbol(candidate);
        if (function.has_body && std::ranges::none_of(candidates, [&](SymbolId other) {
                return other != candidate && derives(symbol(other).owner, function.owner);
            })) {
            return candidate;
        }
    }
    throw std::logic_error("instantiated class " + symbol(cls).name + " lacks an implementation of " + name);
}

bool Converter::derives(SymbolId cls, SymbolId base) const {
    std::vector<SymbolId> pending{cls};
    std::set<SymbolId> seen;
    while (!pending.empty()) {
        const auto scope = pending.back();
        pending.pop_back();
        if (scope == base) {
            return true;
        }
        if (!seen.insert(scope).second) continue;
        const auto& item = symbol(scope);
        if (item.base != no_symbol) pending.push_back(item.base);
        for (const auto& interface : item.interfaces) pending.push_back(interface.symbol);
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

bool Converter::constant_initializer(const IrNode& node) const {
    return node.op == IrOp::Integer || node.op == IrOp::Boolean || node.op == IrOp::String || node.op == IrOp::Float ||
           node.op == IrOp::Nil ||
           (node.op == IrOp::Reference && node.symbol != no_symbol && symbol(node.symbol).kind == SymbolKind::Variant);
}

SymbolId Converter::file_of(SymbolId id) const {
    for (auto scope = id; scope != no_symbol; scope = symbol(scope).owner) {
        if (symbol(scope).kind == SymbolKind::File) {
            return scope;
        }
    }
    return no_symbol;
}

void Converter::order_initialization() {
    // Program initialization (Modules.md, Initialization): globals with
    // literal initializers hold their values when the image is loaded. Every
    // other module or file initializer and every top-level statement runs
    // before the entry function: files that a file imports run first, ties in
    // the order the files were given, and each file runs in source order.
    std::vector<SymbolId> files;
    std::map<SymbolId, std::set<SymbolId>> imports;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = program_.symbols[id];
        if (item.kind == SymbolKind::File) {
            files.push_back(id);
        } else if (item.kind == SymbolKind::Import && item.type.symbol != no_symbol) {
            const auto from = file_of(id);
            const auto to = file_of(item.type.symbol);
            if (from != no_symbol && to != no_symbol && from != to) {
                imports[from].insert(to);
            }
        }
    }
    // Files that import each other, directly or through other files, form a
    // cycle. A cycle runs once every file it imports from outside the cycle
    // has run, and its files run in the given order. Without cycles this is
    // the plain dependency order with ties in the given order.
    std::map<SymbolId, std::set<SymbolId>> reachable;
    for (const auto file : files) {
        auto& seen = reachable[file];
        std::vector<SymbolId> pending(imports[file].begin(), imports[file].end());
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            if (seen.insert(next).second) {
                pending.insert(pending.end(), imports[next].begin(), imports[next].end());
            }
        }
    }
    const auto same_cycle = [&](SymbolId a, SymbolId b) {
        return a == b || (reachable[a].contains(b) && reachable[b].contains(a));
    };
    std::vector<SymbolId> order;
    std::set<SymbolId> placed;
    while (order.size() < files.size()) {
        bool progressed = false;
        for (const auto file : files) {
            if (placed.contains(file)) continue;
            std::vector<SymbolId> cycle;
            for (const auto member : files) {
                if (!placed.contains(member) && same_cycle(file, member)) cycle.push_back(member);
            }
            const bool ready = std::ranges::all_of(cycle, [&](SymbolId member) {
                return std::ranges::all_of(imports[member], [&](SymbolId dependency) {
                    return placed.contains(dependency) || same_cycle(file, dependency);
                });
            });
            if (!ready) continue;
            for (const auto member : cycle) {
                placed.insert(member);
                order.push_back(member);
            }
            progressed = true;
            break;
        }
        if (!progressed) {
            throw std::logic_error("file initialization order has no ready file");
        }
    }
    for (const auto file : order) {
        std::vector<Startup> items;
        for (const auto& body : program_.bodies) {
            const auto kind = symbol(body.owner).kind;
            if ((kind != SymbolKind::File && kind != SymbolKind::Module) || file_of(body.owner) != file) {
                continue;
            }
            for (const auto& instruction : body.instructions) {
                if (instruction.op == IrOp::Initialize && constant_initializer(instruction.operands.front())) {
                    continue;
                }
                items.push_back({&instruction, kind == SymbolKind::Module ? body.owner : no_symbol});
            }
        }
        std::stable_sort(items.begin(), items.end(), [](const Startup& a, const Startup& b) {
            return a.node->span.offset < b.node->span.offset;
        });
        startup_.insert(startup_.end(), items.begin(), items.end());
    }
}

FunctionOrigin Converter::entry_point() const {
    std::set<SymbolId> requirements;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = program_.symbols[id];
        if (item.kind != SymbolKind::Function || item.name != "main" || item.owner == no_symbol) continue;
        const auto& entry = symbol(item.owner);
        if (entry.kind != SymbolKind::Module || entry.name != "Entry" || entry.owner == no_symbol) continue;
        const auto& program = symbol(entry.owner);
        if (program.owner != no_symbol && program.name == "Program" &&
                   symbol(program.owner).library_path == "std/program.nra") {
            const IrType text{TypeKind::String, "string", no_symbol, {}};
            const IrType arguments{TypeKind::Array, {}, no_symbol, {text}};
            if (!item.exported || !entry.exported || item.has_body || item.external ||
                !item.generics.empty() || item.parameters.size() != 1 ||
                symbol(item.parameters.front()).type != arguments ||
                item.type.kind != TypeKind::Enum || !standard_exit_variants(item.type.symbol))
                throw Diagnostic(item.span, "Std.Program.Entry.main must declare main(string[]) -> ExitCode");
            requirements.insert(id);
        }
    }
    std::vector<SymbolId> candidates;
    if (!requirements.empty()) {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = program_.symbols[id];
            if (item.kind != SymbolKind::Function || !item.has_body) {
                continue;
            }
            for (auto base = item.overrides; base != no_symbol; base = symbol(base).overrides) {
                if (requirements.contains(base)) {
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
        throw Diagnostic(where, "the program has no entry point: no module overrides Std.Program.Entry.main");
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
    const auto method = implementation(cls, dispatch.method);
    const auto& item = symbol(method);
    const auto concrete = item.name == "to_string" && symbol(item.owner).name == "$class" ? cls : no_symbol;
    return {Role::Function, method, concrete};
}

Conversion Converter::run() {
    index_program();
    order_initialization();
    if (!options_.entries.empty()) {
        for (const auto entry : options_.entries) {
            const auto owner = symbol(entry).owner;
            function({Role::Start, entry, symbol(owner).kind == SymbolKind::Module ? owner : no_symbol});
        }
    } else {
        // The start function runs initialization, calls Std.Program's main,
        // and maps its ExitCode to the process status.
        const auto entry = entry_point();
        function({Role::Start, entry.symbol, entry.receiver});
    }
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
    thread_jumps();
    remove_unreachable_blocks();
    if (options_.source_map) {
        source_map();
        names();
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
                                              constant(argument.type, immediate_payload(t, argument.bits))});
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

// A block that evaluates nothing and only continues to another block, such
// as the header of `while (true)` or the join after an `if`, costs a step and
// does no work. Every exit that resumes at such a block resumes at its final
// target instead, and the bypassed blocks become unreachable. A chain that
// loops back on itself is left alone, so an empty endless loop still runs.
void Converter::thread_jumps() {
    for (auto& function : image_.functions) {
        const auto count = static_cast<std::uint32_t>(function.blocks.size());
        const auto jump_only = [&](std::uint32_t index) {
            const auto& block = function.blocks[index];
            return block.inputs.empty() && image_.graphs[block.graph].nodes.empty() && block.exits.size() == 1 &&
                   block.exits.front().kind == nvr::ExitKind::Continue && block.exits.front().assignments.empty();
        };
        std::vector<std::uint32_t> forward(count);
        for (std::uint32_t b = 0; b < count; ++b) {
            auto target = b;
            for (std::uint32_t hops = 0; hops <= count && jump_only(target); ++hops) {
                target = function.blocks[target].exits.front().resume;
            }
            // A chain that does not reach a block with work cycles; keep it.
            forward[b] = jump_only(target) ? b : target;
        }
        for (auto& block : function.blocks) {
            for (auto& exit : block.exits) {
                switch (exit.kind) {
                case nvr::ExitKind::Continue:
                case nvr::ExitKind::Call:
                case nvr::ExitKind::State:
                    exit.resume = forward[exit.resume];
                    break;
                case nvr::ExitKind::Host:
                    exit.resume = forward[exit.resume];
                    exit.error_resume = forward[exit.error_resume];
                    break;
                default:
                    break;
                }
            }
        }
    }
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

} // namespace

std::string source_label(const IrProgram& program, const std::string& file) {
    namespace fs = std::filesystem;
    // A trusted standard library file is labeled by its path below the
    // library root, such as std/core/optional.nra, so an image never records
    // where the library was installed. Program files are labeled relative to
    // the common directory of the program's own files.
    for (const auto& item : program.symbols) {
        if (item.kind == SymbolKind::File && item.name == file && !item.library_path.empty()) {
            return item.library_path;
        }
    }
    std::optional<fs::path> root;
    for (const auto& item : program.symbols) {
        if (item.kind != SymbolKind::File || !item.library_path.empty()) {
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

namespace {

std::string Converter::label(const std::string& file) const {
    return source_label(program_, file);
}

std::string Converter::qualified(SymbolId id) const {
    std::string name = symbol(id).name;
    for (auto owner = symbol(id).owner; owner != no_symbol && symbol(owner).kind != SymbolKind::File;
         owner = symbol(owner).owner) {
        if (!is_static_scope(symbol(owner))) name = symbol(owner).name + "." + name;
    }
    return name;
}

void Converter::names() {
    // Provenance for tools: the NAME section, emitted with the source map.
    nvr::Names names;
    for (const auto& origin : origins_) {
        switch (origin.role) {
        case Role::Function: names.functions.push_back(qualified(origin.symbol)); break;
        case Role::Constructor: names.functions.push_back(qualified(origin.symbol) + ".init"); break;
        case Role::Destructor: names.functions.push_back(qualified(origin.symbol) + ".deinit"); break;
        case Role::Concatenate: names.functions.push_back("string +"); break;
        case Role::HashText: names.functions.push_back("string hash_code"); break;
        case Role::ValueEquals: names.functions.push_back("aggregate equals"); break;
        case Role::ValueHash: names.functions.push_back("aggregate hash_code"); break;
        case Role::ValueText: names.functions.push_back("aggregate to_string"); break;
        case Role::Start: names.functions.push_back("start " + qualified(origin.symbol)); break;
        }
    }
    names.layouts.resize(image_.layouts.size());
    names.fields.resize(image_.layouts.size());
    for (const auto& [cls, layout] : layouts_) {
        names.layouts[layout] = qualified(cls);
        names.fields[layout].resize(image_.layouts[layout].fields.size());
        for (auto scope = cls; scope != no_symbol; scope = symbol(scope).base) {
            for (const auto& [field, index] : fields_) {
                if (symbol(field).owner == scope && index < names.fields[layout].size()) {
                    names.fields[layout][index] = symbol(field).name;
                }
            }
        }
    }
    names.globals.resize(image_.globals.size());
    for (const auto& [global, index] : globals_) {
        names.globals[index] = qualified(global);
    }
    names.slots.resize(image_.functions.size());
    for (auto& [function, slots] : slot_names_) {
        names.slots[function] = std::move(slots);
    }
    image_.other_sections.push_back(nvr::encode_names(names));
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
    block_.emplace(c_);
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
    if (const auto it = block_->synth.pending().find(slot); it != block_->synth.pending().end()) {
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
    return current().synth.lanes(resolve(original));
}

void FunctionCompiler::spill(Operand& operand) {
    if (operand.kind != Operand::Kind::Bits || operand.generation != generation_ || operand.home != absent) {
        return;
    }
    operand.home = new_slot(operand.type);
    current().synth.pending()[operand.home] = operand;
}

void FunctionCompiler::assign(std::uint32_t slot, Operand operand) {
    operand = resolve(operand);
    if (operand.kind == Operand::Kind::Void) {
        throw std::logic_error("cannot assign a void value");
    }
    current().synth.pending()[slot] = std::move(operand);
}

void FunctionCompiler::operation_at(const Span& span) {
    current().operation_span = span;
}

void FunctionCompiler::fail_when(const Signal& condition, std::uint32_t reason, const Span& span) {
    if (current().synth.fail_when(condition, reason) && !current().failure_span) {
        current().failure_span = span;
    }
}

void FunctionCompiler::close(std::vector<ExitSpec> exits) {
    auto& open_block = current();
    for (auto& exit : exits) {
        for (auto& [slot, operand] : exit.assignments) {
            operand = resolve(operand);
        }
        for (auto& argument : exit.arguments) {
            argument = resolve(argument);
        }
    }
    auto block = open_block.synth.close(std::move(exits));
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
    std::vector<std::string> slot_names(function_.frame_types.size());
    for (const auto& [symbol, slot] : slots_) {
        if (slot < slot_names.size() && !c_.symbol(symbol).name.starts_with("$")) {
            slot_names[slot] = c_.symbol(symbol).name;
        }
    }
    if (self_slot_ != absent && self_slot_ < slot_names.size()) {
        slot_names[self_slot_] = "this";
    }
    c_.name_slots(index_, std::move(slot_names));
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
        if (c_.symbol(node.symbol).kind == SymbolKind::Global && origin_.role == Role::Start) {
            auto value = expression(node.operands.front());
            state(nvr::StateOp::WriteGlobal, c_.global(node.symbol, node.span), {value},
                  c_.scalar(nvr::TypeKind::Void), node.span);
            return;
        }
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
        jump(loops_.back().next);
        return;
    case IrOp::Fail: {
        // The only failure NeuraIR names is a list changed during for-each.
        ExitSpec trap;
        trap.kind = nvr::ExitKind::Trap;
        trap.reason = iteration_failure;
        operation_at(node.span);
        close({trap});
        return;
    }
    case IrOp::TrapMessage: {
        auto message = expression(node.operands.front());
        ExitSpec trap;
        trap.kind = nvr::ExitKind::MessageTrap;
        trap.arguments.push_back(std::move(message));
        operation_at(node.span);
        close({trap});
        return;
    }
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
    // A latch runs after the body and on continue, then the condition is
    // tested again. A loop without one continues straight at its head.
    const auto latch = node.alternative.empty() ? head : new_block();
    loops_.push_back({latch, exit});
    begin(body, node.body.empty() ? node.span : node.body.front().span);
    region(node.body);
    if (open()) {
        jump(latch);
    }
    loops_.pop_back();
    if (latch != head) {
        begin(latch, node.alternative.front().span);
        region(node.alternative);
        if (open()) {
            jump(head);
        }
    }
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
    case IrOp::Float:
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

// True when evaluating `node` can move arithmetic into a block of its own:
// an integer multiplication, division, or remainder, or floating-point
// arithmetic, whose operands are not both known to be immediates. Such an
// evaluation closes the current block.
bool FunctionCompiler::may_isolate(const IrNode& node) {
    const auto literal = [](const IrNode& operand) {
        return operand.op == IrOp::Integer || operand.op == IrOp::Float;
    };
    if (node.op == IrOp::Binary && node.operands.size() == 2 && !literal(node.operands[0]) &&
        !literal(node.operands[1])) {
        const bool floating = node.operands[0].type.kind == TypeKind::Float && !is_comparison(node.text);
        if (floating || node.text == "*" || node.text == "/" || node.text == "%") {
            return true;
        }
    }
    for (const auto* region : {&node.operands, &node.body}) {
        for (const auto& child : *region) {
            if (may_isolate(child)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<Operand> FunctionCompiler::sequence_of(const std::vector<const IrNode*>& nodes) {
    // Operands are evaluated left to right. A graph value that must outlive
    // a later operand's block is spilled to a temporary slot first.
    std::vector<Operand> result;
    result.reserve(nodes.size());
    for (const auto* node : nodes) {
        if (!graph_only(*node) || may_isolate(*node)) {
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
    case IrOp::ValueEquals:
    case IrOp::ValueHash:
    case IrOp::ValueText:
        return value_member(node.op, node.operands.front().type, sequence(node.operands), node.span);
    case IrOp::Nil:
        return immediate(c_.type_of(node.type, node.span), 0);
    case IrOp::ClassHash:
        return state(nvr::StateOp::ClassHash, 0, {expression(node.operands.front())},
                     c_.type_of(node.type, node.span), node.span);
    case IrOp::ClassText: {
        const auto object = expression(node.operands.front());
        Operand name;
        name.kind = Operand::Kind::Constant;
        name.type = c_.scalar(nvr::TypeKind::Text);
        const auto cls = origin_.receiver == no_symbol ? node.operands.front().type.symbol : origin_.receiver;
        name.index = c_.text_constant(c_.qualified(cls), node.span);
        return state(nvr::StateOp::ClassText, 0, {object, name}, name.type, node.span);
    }
    case IrOp::ClassIs:
    case IrOp::ClassCast: {
        const auto object = expression(node.operands.front());
        const bool test = node.op == IrOp::ClassIs;
        const auto& target = test ? node.type_arguments.front() : node.type;
        return state(test ? nvr::StateOp::ClassIs : nvr::StateOp::ClassCast,
                     c_.layout(target.symbol, node.span), {object},
                     c_.type_of(node.type, node.span), node.span);
    }
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
    case IrOp::Float: {
        const auto type = c_.type_of(node.type, node.span);
        return immediate(type, c_.literal(node, type).value());
    }
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
    case IrOp::ComparisonChain:
        statement(node.body.front());
        return expression(node.body.back());
    case IrOp::ScopedValue:
        for (std::size_t i = 0; i + 1 < node.body.size(); ++i) {
            statement(node.body[i]);
            // An unconditional trap can make the remaining expression
            // unreachable. Its disconnected blocks are removed at the end.
            if (!open()) begin(new_block(), node.span);
        }
        return expression(node.body.back());
    case IrOp::SelectValue:
        return select_value(node);
    case IrOp::Call:
    case IrOp::VirtualCall:
    case IrOp::ExternalCall:
        return call(node);
    case IrOp::MakeClosure:
    case IrOp::CallClosure:
        throw Diagnostic(node.span, "function values must be specialized before conversion");
    case IrOp::Allocate:
        return allocate(node);
    case IrOp::Tuple:
    case IrOp::EnumConstruct:
        return aggregate(node);
    case IrOp::Project:
    case IrOp::EnumTag:
    case IrOp::EnumProject: {
        const auto object = expression(node.operands.front());
        std::uint32_t field = 0;
        if (node.op != IrOp::EnumTag)
            field = static_cast<std::uint32_t>(aggregate_index(node.text).value());
        if (node.op == IrOp::EnumProject)
            field += c_.payload_start(node.symbol, node.span);
        return state(nvr::StateOp::ReadField, field, {object}, c_.type_of(node.type, node.span), node.span);
    }
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
    case IrOp::TextLength:
    case IrOp::Length: {
        // ReadLength yields uint64; `length` is int32. Lengths beyond int32
        // cannot occur within NVM's heap limits, but the narrowing is still
        // checked.
        auto collection = expression(node.operands.front());
        const auto u64 = c_.scalar(nvr::TypeKind::Unsigned, 64);
        const auto length = state(nvr::StateOp::ReadLength, 0, {collection}, u64, node.span);
        auto narrowed = convert_integer(current().synth.circuit(), lanes(length), false, 32, true);
        fail_when(narrowed.failure, arithmetic_failure, node.span);
        return bits(c_.type_of(node.type, node.span), std::move(narrowed.value));
    }
    case IrOp::TextByteAt:
    case IrOp::TextSlice: {
        auto arguments = sequence(node.operands);
        for (std::size_t i = 1; i < arguments.size(); ++i)
            arguments[i] = to_index(arguments[i], node.operands[i].type, node.span);
        const auto operation = node.op == IrOp::TextSlice ? nvr::StateOp::TextSlice : nvr::StateOp::ReadElement;
        return state(operation, 0, std::move(arguments), c_.type_of(node.type, node.span), node.span);
    }
    case IrOp::TextFromUtf8: {
        auto input = expression(node.operands.front());
        const auto result = c_.type_of(node.type, node.span);
        return state(nvr::StateOp::TextFromUtf8, c_.type_at(result).reference, {input}, result, node.span);
    }
    case IrOp::TextEqual: {
        auto arguments = sequence(node.operands);
        const auto boolean = c_.scalar(nvr::TypeKind::Bool);
        auto equal = state(nvr::StateOp::TextEqual, 0, std::move(arguments), boolean, node.span);
        if (node.text == "==") return equal;
        return bits(boolean, {Circuit::logical_not(lanes(equal).front())});
    }
    case IrOp::ReinterpretFloat: {
        auto input = expression(node.operands.front());
        return bits(c_.type_of(node.type, node.span), lanes(input));
    }
    case IrOp::CollectionVersion: {
        auto list = expression(node.operands.front());
        return state(nvr::StateOp::ReadVersion, 0, {list}, c_.scalar(nvr::TypeKind::Unsigned, 64), node.span);
    }
    case IrOp::Convert:
        return convert(node);
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

SymbolId FunctionCompiler::resolve_global(const IrNode& node) {
    if (node.operands.empty()) {
        return node.symbol;
    }
    const auto& qualifier = node.operands.front();
    if (qualifier.type.kind == TypeKind::Module)
        return c_.resolve_member(module_of(qualifier), node.symbol);
    expression(qualifier);
    return node.symbol;
}

Operand FunctionCompiler::unary(const IrNode& node) {
    const auto operand = expression(node.operands.front());
    if (node.text == "!") {
        return bits(c_.scalar(nvr::TypeKind::Bool), {Circuit::logical_not(lanes(operand).front())});
    }
    if (node.text == "~") {
        auto word = lanes(operand);
        std::ranges::transform(word, word.begin(), Circuit::logical_not);
        return bits(c_.type_of(node.type, node.span), std::move(word));
    }
    if (node.type.kind == TypeKind::Float) {
        return bits(c_.type_of(node.type, node.span), float_negate(lanes(operand)));
    }
    const auto format = integer_format(node.type, node.span);
    const auto input = lanes(operand);
    auto result = negate(current().synth.circuit(), input, format.is_signed);
    fail_when(result.failure, arithmetic_failure, node.span);
    return bits(c_.type_of(node.type, node.span), std::move(result.value));
}

Operand FunctionCompiler::binary(const IrNode& node) {
    const auto& op = node.text;
    const auto& operand_type = node.operands.front().type;
    if (operand_type.kind == TypeKind::Object) {
        auto operands = sequence(node.operands);
        const auto left = lanes(operands[0]);
        const auto right = lanes(operands[1]);
        const auto same = equal(current().synth.circuit(), left, right);
        return bits(c_.scalar(nvr::TypeKind::Bool), {op == "==" ? same : Circuit::logical_not(same)});
    }
    if (operand_type.kind == TypeKind::String) {
        auto operands = sequence(node.operands);
        return invoke({Role::Concatenate, no_symbol, no_symbol}, std::move(operands),
                      c_.scalar(nvr::TypeKind::Text), node.span);
    }
    auto operands = sequence(node.operands);
    if (operand_type.kind == TypeKind::Float) {
        return float_binary(node, operands[0], operands[1]);
    }
    if ((op == "*" || op == "/" || op == "%") && operands[0].kind != Operand::Kind::Immediate &&
        operands[1].kind != Operand::Kind::Immediate) {
        return isolated_arithmetic(node, operands[0], operands[1], integer_format(operand_type, node.span));
    }
    const auto a = lanes(operands[0]);
    const auto b = lanes(operands[1]);
    auto& circuit = current().synth.circuit();
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    if (operand_type.kind == TypeKind::Bool) {
        const auto same = Circuit::logical_not(circuit.logical_xor(a.front(), b.front()));
        return bits(boolean, {op == "==" ? same : Circuit::logical_not(same)});
    }
    const auto format = integer_format(operand_type, node.span);
    if (is_shift(op)) {
        auto result = shift(circuit, a, b, op == ">>", format.is_signed);
        fail_when(result.failure, arithmetic_failure, node.span);
        return bits(c_.type_of(node.type, node.span), std::move(result.value));
    }
    if (is_bitwise_binary(op)) {
        Word result;
        result.reserve(a.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (op == "&") {
                result.push_back(circuit.logical_and(a[i], b[i]));
            } else if (op == "|") {
                result.push_back(circuit.logical_or(a[i], b[i]));
            } else {
                result.push_back(circuit.logical_xor(a[i], b[i]));
            }
        }
        return bits(c_.type_of(node.type, node.span), std::move(result));
    }
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
    } else if (op == "%") {
        result = remainder(circuit, a, b, format.is_signed);
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
    auto& circuit = current().synth.circuit();
    auto result = node.text == "*"   ? multiply(circuit, a, b, format.is_signed)
                  : node.text == "/" ? divide(circuit, a, b, format.is_signed)
                                     : remainder(circuit, a, b, format.is_signed);
    fail_when(result.failure, arithmetic_failure, node.span);
    auto value = bits(c_.type_of(node.type, node.span), std::move(result.value));
    spill(value);
    const auto next = new_block();
    jump(next);
    begin(next, node.span);
    return resolve(value);
}

FloatFormat FunctionCompiler::float_of(const IrType& type) const {
    return float_format(type).value();
}

Operand FunctionCompiler::float_binary(const IrNode& node, Operand left, Operand right) {
    // Arithmetic between two variables gets a block of its own, like integer
    // multiplication: the circuits are large and every site with the same
    // operation and format shares one graph. Comparisons stay inline.
    const auto& op = node.text;
    const auto format = float_of(node.operands.front().type);
    const auto compute = [&](Circuit& circuit, const Word& a, const Word& b) {
        if (op == "+") return float_add(circuit, a, b, format);
        if (op == "-") return float_subtract(circuit, a, b, format);
        if (op == "*") return float_multiply(circuit, a, b, format);
        return float_divide(circuit, a, b, format);
    };
    if (is_comparison(op)) {
        const auto comparison = op == "==" ? FloatComparison::Equal
                              : op == "!=" ? FloatComparison::NotEqual
                              : op == "<"  ? FloatComparison::Less
                              : op == "<=" ? FloatComparison::LessEqual
                              : op == ">"  ? FloatComparison::Greater
                                           : FloatComparison::GreaterEqual;
        const auto a = lanes(left);
        const auto b = lanes(right);
        return bits(c_.scalar(nvr::TypeKind::Bool), {float_compare(current().synth.circuit(), a, b, format, comparison)});
    }
    const auto type = c_.type_of(node.type, node.span);
    if (left.kind == Operand::Kind::Immediate || right.kind == Operand::Kind::Immediate) {
        const auto a = lanes(left);
        const auto b = lanes(right);
        return bits(type, compute(current().synth.circuit(), a, b));
    }
    spill(left);
    spill(right);
    const auto own = new_block();
    jump(own);
    begin(own, node.span);
    const auto a = lanes(left);
    const auto b = lanes(right);
    auto value = bits(type, compute(current().synth.circuit(), a, b));
    spill(value);
    const auto next = new_block();
    jump(next);
    begin(next, node.span);
    return resolve(value);
}

Operand FunctionCompiler::convert(const IrNode& node) {
    const auto& source = node.operands.front();
    const auto operand = expression(source);
    auto& circuit = current().synth.circuit();
    const auto type = c_.type_of(node.type, node.span);
    if (source.type.kind == TypeKind::Float && node.type.kind == TypeKind::Float) {
        return bits(type, float_convert(circuit, lanes(operand), float_of(source.type), float_of(node.type)));
    }
    if (node.type.kind == TypeKind::Float) {
        const auto from = integer_format(source.type, source.span);
        return bits(type, float_from_integer(circuit, lanes(operand), from.is_signed, float_of(node.type)));
    }
    const auto to = integer_format(node.type, node.span);
    Checked result;
    if (source.type.kind == TypeKind::Float) {
        result = float_to_integer(circuit, lanes(operand), float_of(source.type), to.bits, to.is_signed);
    } else {
        const auto from = integer_format(source.type, source.span);
        result = convert_integer(circuit, lanes(operand), from.is_signed, to.bits, to.is_signed);
    }
    fail_when(result.failure, arithmetic_failure, node.span);
    return bits(type, std::move(result.value));
}

Operand FunctionCompiler::scalar_member(IrOp op, const IrType& type, std::vector<Operand> operands,
                                        const Span& span) {
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    const auto hash_type = c_.scalar(nvr::TypeKind::Unsigned, 32);
    if (type.kind == TypeKind::String) {
        if (op == IrOp::ValueText) return operands.front();
        if (op == IrOp::ValueEquals)
            return state(nvr::StateOp::TextEqual, 0, std::move(operands), boolean, span);
        return invoke({Role::HashText, no_symbol, no_symbol}, std::move(operands), hash_type, span);
    }
    if (op == IrOp::ValueText) {
        const auto text = c_.scalar(nvr::TypeKind::Text);
        if (type.kind == TypeKind::Enum) {
            for (const auto& variant : c_.program().symbols) {
                if (variant.kind != SymbolKind::Variant || variant.owner != type.symbol) continue;
                Operand name;
                name.kind = Operand::Kind::Constant;
                name.type = text;
                name.index = c_.text_constant(variant.name, span);
                operands.push_back(std::move(name));
            }
        }
        return state(nvr::StateOp::ScalarText, 0, std::move(operands), text, span);
    }

    auto& circuit = current().synth.circuit();
    const auto normalized = [&](const Operand& operand) {
        auto word = lanes(operand);
        if (type.kind != TypeKind::Float) return word;
        const auto format = float_of(type);
        const auto first_exponent = word.begin() + format.fraction_bits;
        const auto nan = circuit.logical_and(circuit.all({first_exponent, word.end() - 1}),
                                             circuit.any({word.begin(), first_exponent}));
        const auto canonical = constant_word(format.canonical_nan(), format.width());
        for (std::size_t bit = 0; bit < word.size(); ++bit) {
            word[bit] = circuit.choose(nan, canonical[bit], word[bit]);
        }
        return word;
    };
    auto first = normalized(operands.front());
    if (op == IrOp::ValueEquals)
        return bits(boolean, {equal(circuit, first, normalized(operands[1]))});

    if (type.kind == TypeKind::Bool) {
        const auto true_hash = constant_word(1231, 32);
        const auto false_hash = constant_word(1237, 32);
        Word hash;
        for (std::size_t bit = 0; bit < 32; ++bit)
            hash.push_back(circuit.choose(first.front(), true_hash[bit], false_hash[bit]));
        return bits(hash_type, std::move(hash));
    }
    if (first.size() == 64) {
        for (std::size_t bit = 0; bit < 32; ++bit)
            first[bit] = circuit.logical_xor(first[bit], first[bit + 32]);
        first.resize(32);
    } else if (first.size() < 32) {
        const bool signed_integer = type.kind == TypeKind::Integer && integer_format(type, span).is_signed;
        const auto extension = signed_integer ? first.back() : Signal::of(0);
        first.resize(32, extension);
    }
    return bits(hash_type, std::move(first));
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
        auto& circuit = current().synth.circuit();
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
    if (owner != no_symbol && (c_.symbol(owner).kind == SymbolKind::Class ||
                               c_.symbol(owner).kind == SymbolKind::Interface)) {
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
    return dispatch_call(receiver.type.symbol, node.symbol, sequence(node.operands), result_type, node.span);
}

Operand FunctionCompiler::dispatch_call(SymbolId cls, SymbolId method, std::vector<Operand> operands,
                                        std::uint32_t result_type, const Span& span) {
    for (auto& operand : operands) {
        spill(operand);
    }
    if (operands.front().kind != Operand::Kind::Slot && operands.front().home == absent) {
        const auto slot = new_slot(operands.front().type);
        assign(slot, operands.front());
        Operand saved;
        saved.kind = Operand::Kind::Slot;
        saved.type = operands.front().type;
        saved.index = slot;
        operands.front() = saved;
        const auto read = new_block();
        jump(read);
        begin(read, span);
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
    operation_at(span);
    close({read_tag});
    for (auto& operand : operands) {
        operand = resolve(operand);
    }
    spans_[dispatch_block] = span;
    Dispatch dispatch{index_, dispatch_block, cls, method, tag_slot, operands.front(),
                      std::vector<Operand>(operands.begin() + 1, operands.end()), result_slot, resume};
    c_.add_dispatch(std::move(dispatch));
    begin(resume, span);
    return result_slot == absent ? Operand{} : slot_operand(result_slot);
}

// The source spelling of a value member's operand type, for diagnostics.
std::string describe_value_type(const IrType& type) {
    if ((type.kind == TypeKind::Array || type.kind == TypeKind::List) && type.arguments.size() == 1) {
        return describe_value_type(type.arguments.front()) + (type.kind == TypeKind::Array ? "[]" : "[list]");
    }
    if (type.kind == TypeKind::Tuple) {
        std::string text = "[";
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            text += (i == 0 ? "" : ", ") + describe_value_type(type.arguments[i]);
        }
        return text + "]";
    }
    return type.name;
}

Operand FunctionCompiler::value_member(IrOp op, const IrType& type, std::vector<Operand> operands, const Span& span) {
    const bool equality = op == IrOp::ValueEquals;
    // A generic value member reaches conversion with its specialized operand
    // type. Only reachable code is converted, so an unused generic member,
    // such as an interface method copied for dispatch, is not rejected.
    if (!supports_value_members(c_.program(), type)) {
        const std::string member = equality ? "equals" : op == IrOp::ValueHash ? "hash_code" : "to_string";
        throw Diagnostic(span, "type " + describe_value_type(type) + " has no member '" + member +
                                   "' in this specialization");
    }
    const auto result_type = equality ? c_.scalar(nvr::TypeKind::Bool) :
        op == IrOp::ValueHash ? c_.scalar(nvr::TypeKind::Unsigned, 32) : c_.scalar(nvr::TypeKind::Text);
    if (type.kind == TypeKind::Tuple || type.kind == TypeKind::Array || type.kind == TypeKind::List)
        return invoke(c_.value_function(op, type, span), std::move(operands), result_type, span);
    if (type.kind != TypeKind::Object) return scalar_member(op, type, std::move(operands), span);

    const auto cls = type.symbol;
    if (c_.symbol(cls).name.starts_with("$signature"))
        throw Diagnostic(span, "function values do not support equality, hashing, or text members");
    auto root = cls;
    while (c_.symbol(root).base != no_symbol) root = c_.symbol(root).base;
    const auto method = c_.member(root, equality ? "equals" : op == IrOp::ValueHash ? "hash_code" : "to_string");
    if (method == no_symbol) throw Diagnostic(span, "this class reference has no value member contract");
    for (auto& operand : operands) spill(operand);
    const auto is_nil = [&](const Operand& operand) {
        const auto word = lanes(operand);
        return Circuit::logical_not(current().synth.circuit().any(word));
    };
    const auto left_nil = is_nil(operands[0]);
    const auto right_nil = equality ? is_nil(operands[1]) : Signal::of(0);
    const auto nil = current().synth.circuit().logical_or(left_nil, right_nil);
    auto fallback = immediate(result_type, 0);
    if (equality) {
        fallback = bits(result_type, {current().synth.circuit().logical_and(left_nil, right_nil)});
        spill(fallback);
    } else if (op == IrOp::ValueText) {
        fallback.kind = Operand::Kind::Constant;
        fallback.index = c_.text_constant("nil", span);
    }
    const auto handle_nil = new_block();
    const auto invoke_method = new_block();
    const auto done = new_block();
    const auto slot = new_slot(result_type);
    branch(bits(c_.scalar(nvr::TypeKind::Bool), {nil}), handle_nil, invoke_method);
    begin(handle_nil, span);
    assign(slot, fallback);
    jump(done);
    begin(invoke_method, span);
    auto result = dispatch_call(cls, method, std::move(operands), result_type, span);
    assign(slot, result);
    jump(done);
    begin(done, span);
    return slot_operand(slot);
}

Operand FunctionCompiler::external_call(const IrNode& node) {
    if (c_.symbol(node.symbol).host_binding) return bound_host_call(node);
    const auto& target = c_.symbol(node.symbol);
    throw Diagnostic(node.span, "external function '" + c_.symbol(target.owner).name + "." + target.name +
                                    "' has no runtime binding");
}

Operand FunctionCompiler::bound_host_call(const IrNode& node) {
    auto arguments = sequence(node.operands);
    const auto import_index = c_.external_import(node.symbol, node.span).value();
    const auto& import = c_.import_at(import_index);
    const auto variants = host_variants(c_.program(), node.type).value();
    const auto success_type = import.success_type;
    const auto error_type = import.error_type;
    const auto success = new_block();
    const auto failure = new_block();
    const auto join = new_block();
    const auto result = new_slot(c_.type_of(node.type, node.span));
    const auto value = c_.type_at(success_type).kind == nvr::TypeKind::Void ? absent : new_slot(success_type);
    const auto error = new_slot(error_type);

    ExitSpec host;
    host.kind = nvr::ExitKind::Host;
    host.target = import_index;
    host.arguments = std::move(arguments);
    host.resume = success;
    host.result_slot = value;
    host.error_slot = error;
    host.error_resume = failure;
    operation_at(node.span);
    close({host});

    const auto emit_result = [&](std::uint32_t block, SymbolId variant, std::uint32_t slot) {
        begin(block, node.span);
        std::vector<Operand> fields;
        if (slot != absent) fields.push_back(slot_operand(slot));
        assign(result, aggregate_value(node.type, variant, std::move(fields), {}, node.span));
        jump(join);
    };
    emit_result(success, variants.success, value);
    emit_result(failure, variants.error, error);
    begin(join, node.span);
    return slot_operand(result);
}

Operand FunctionCompiler::select_value(const IrNode& node) {
    auto condition = expression(node.operands.front());
    const auto when_true = new_block();
    const auto when_false = new_block();
    const auto join = new_block();
    const auto result = new_slot(c_.type_of(node.type, node.span));
    branch(condition, when_true, when_false);
    const auto emit_arm = [&](std::uint32_t block, const IrNode& arm) {
        begin(block, arm.span);
        assign(result, expression(arm));
        jump(join);
    };
    emit_arm(when_true, node.body.front());
    emit_arm(when_false, node.alternative.front());
    begin(join, node.span);
    return slot_operand(result);
}

Operand FunctionCompiler::aggregate(const IrNode& node) {
    return aggregate_value(node.type, node.op == IrOp::EnumConstruct ? node.symbol : no_symbol,
                           sequence(node.operands), node.text, node.span);
}

Operand FunctionCompiler::aggregate_value(const IrType& value_type, SymbolId variant, std::vector<Operand> fields,
                                           std::string_view field_order, const Span& span) {
    for (auto& field : fields) spill(field);
    const auto type = c_.type_of(value_type, span);
    const auto layout = c_.type_at(type).reference;
    const auto handle = state(nvr::StateOp::AllocateObject, layout, {}, type, span);
    const auto nothing = c_.scalar(nvr::TypeKind::Void);
    if (variant != no_symbol) {
        const auto tag_type = c_.scalar(nvr::TypeKind::Unsigned, 32);
        const auto tag = immediate(tag_type, c_.ordinal(variant));
        state(nvr::StateOp::WriteField, 0, {handle, tag}, nothing, span);
        const auto start = c_.payload_start(variant, span);
        const auto order = enum_field_order(field_order, fields.size()).value();
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto index = start + static_cast<std::uint32_t>(order[i]);
            state(nvr::StateOp::WriteField, index, {handle, resolve(fields[i])}, nothing, span);
        }
    } else {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            state(nvr::StateOp::WriteField, static_cast<std::uint32_t>(i),
                  {handle, resolve(fields[i])}, nothing, span);
        }
    }
    return handle;
}

Operand FunctionCompiler::allocate(const IrNode& node) {
    const auto cls = node.symbol == no_symbol ? node.type.symbol : c_.symbol(node.symbol).owner;
    const auto type = c_.type_of(c_.symbol(cls).type, node.span);
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
    if (owner != no_symbol && (c_.symbol(owner).kind == SymbolKind::Class ||
                               c_.symbol(owner).kind == SymbolKind::Interface)) {
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
    const bool has_base_initializer = item.base != no_symbol && c_.symbol(item.base).name != "$class";
    if (init != no_symbol) {
        parameters(init);
    }
    function_.parameter_count = static_cast<std::uint32_t>(function_.frame_types.size());
    function_.result_type = c_.scalar(nvr::TypeKind::Void);
    begin(new_block(), item.span);
    if (init != no_symbol) {
        const auto& body = c_.body(init);
        std::size_t start = 0;
        if (has_base_initializer && !body.empty()) {
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
        if (has_base_initializer) {
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
    if (item.base != no_symbol && c_.symbol(item.base).name != "$class") {
        invoke({Role::Destructor, item.base, no_symbol}, {slot_operand(self_slot_)}, void_type, item.span);
    }
    finish();
}

void FunctionCompiler::compile_concatenate() {
    // Copies the UTF-8 bytes of both operands into a uint8 list and builds
    // text from it. Concatenating valid UTF-8 yields valid UTF-8.
    const auto text = c_.scalar(nvr::TypeKind::Text);
    const auto u8 = c_.scalar(nvr::TypeKind::Unsigned, 8);
    const auto list = c_.type_index({nvr::TypeKind::List, 0, u8});
    const std::uint32_t sources[] = {new_slot(text), new_slot(text)};
    function_.parameter_count = 2;
    function_.result_type = text;
    const Span none;
    begin(new_block(), none);
    const auto buffer = state(nvr::StateOp::AllocateCollection, list, {}, list, none);
    for (const auto source : sources) append_text_bytes(buffer, slot_operand(source), none);
    const auto result = state(nvr::StateOp::ConstructText, text, {buffer}, text, none);
    ExitSpec exit;
    exit.kind = nvr::ExitKind::Return;
    exit.arguments = {result};
    close({exit});
    finish();
}

void FunctionCompiler::append_text_bytes(const Operand& buffer, const Operand& source, const Span& span) {
    const auto u8 = c_.scalar(nvr::TypeKind::Unsigned, 8);
    const auto u64 = c_.scalar(nvr::TypeKind::Unsigned, 64);
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    const auto void_type = c_.scalar(nvr::TypeKind::Void);
    const auto length = state(nvr::StateOp::ReadLength, 0, {source}, u64, span);
    const auto position = new_slot(u64);
    assign(position, immediate(u64, 0));
    const auto head = new_block();
    jump(head);
    begin(head, span);
    const auto more = less(current().synth.circuit(), lanes(slot_operand(position)), lanes(length), false);
    const auto body = new_block();
    const auto next = new_block();
    branch(bits(boolean, {more}), body, next);
    begin(body, span);
    const auto byte = state(nvr::StateOp::ReadElement, 0, {source, slot_operand(position)}, u8, span);
    auto incremented = add(current().synth.circuit(), lanes(slot_operand(position)), constant_word(1, 64), false);
    assign(position, bits(u64, std::move(incremented.value)));
    state(nvr::StateOp::AppendElement, 0, {buffer, byte}, void_type, span);
    jump(head);
    begin(next, span);
}

void FunctionCompiler::compile_value_member(IrOp op) {
    const auto [type, span] = c_.value_type(origin_.symbol);
    const bool equality = op == IrOp::ValueEquals;
    const bool hashing = op == IrOp::ValueHash;
    const bool tuple = type.kind == TypeKind::Tuple;
    const auto input = c_.type_of(type, span);
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    const auto hash_type = c_.scalar(nvr::TypeKind::Unsigned, 32);
    const auto text = c_.scalar(nvr::TypeKind::Text);
    const auto byte = c_.scalar(nvr::TypeKind::Unsigned, 8);
    const auto count = c_.scalar(nvr::TypeKind::Unsigned, 64);
    const auto none = c_.scalar(nvr::TypeKind::Void);
    const auto left = new_slot(input);
    const auto right = equality ? new_slot(input) : absent;
    function_.parameter_count = equality ? 2 : 1;
    function_.result_type = equality ? boolean : hashing ? hash_type : text;
    begin(new_block(), span);

    const auto negative = equality ? new_block() : absent;
    const auto hash = hashing ? new_slot(hash_type) : absent;
    Operand buffer;
    if (hashing) assign(hash, immediate(hash_type, 1));
    if (!equality && !hashing) {
        const auto list = c_.type_index({nvr::TypeKind::List, 0, byte});
        buffer = state(nvr::StateOp::AllocateCollection, list, {immediate(byte, '[')}, list, span);
    }
    const auto append = [&](unsigned char value) {
        state(nvr::StateOp::AppendElement, 0, {buffer, immediate(byte, value)}, none, span);
    };
    const auto require_equal = [&](const Operand& value) {
        const auto next = new_block();
        branch(value, next, negative);
        begin(next, span);
    };
    const auto process = [&](const IrType& field, std::uint32_t position, const Operand& index) {
        const auto read = [&](std::uint32_t slot) {
            std::vector values{slot_operand(slot)};
            if (!tuple) values.push_back(index);
            return state(tuple ? nvr::StateOp::ReadField : nvr::StateOp::ReadElement,
                         tuple ? position : 0, std::move(values), c_.type_of(field, span), span);
        };
        std::vector operands{read(left)};
        if (equality) operands.push_back(read(right));
        const auto value = value_member(op, field, std::move(operands), span);
        if (equality) {
            require_equal(value);
        } else if (hashing) {
            auto word = lanes(slot_operand(hash));
            Word times_32(5, Signal::of(0));
            times_32.insert(times_32.end(), word.begin(), word.end() - 5);
            auto& circuit = current().synth.circuit();
            const auto product = subtract(circuit, times_32, word, false).value;
            assign(hash, bits(hash_type, add(circuit, product, lanes(value), false).value));
        } else {
            append_text_bytes(buffer, value, span);
        }
    };
    if (tuple) {
        for (std::uint32_t i = 0; i < type.arguments.size(); ++i) {
            if (!equality && !hashing && i != 0) {
                append(',');
                append(' ');
            }
            process(type.arguments[i], i, {});
        }
    } else {
        const auto length = state(nvr::StateOp::ReadLength, 0, {slot_operand(left)}, count, span);
        if (equality) {
            const auto other = state(nvr::StateOp::ReadLength, 0, {slot_operand(right)}, count, span);
            require_equal(bits(boolean, {equal(current().synth.circuit(), lanes(length), lanes(other))}));
        }
        const auto position = new_slot(count);
        assign(position, immediate(count, 0));
        const auto head = new_block();
        const auto next = new_block();
        const auto done = new_block();
        jump(head);
        begin(head, span);
        branch(bits(boolean, {less(current().synth.circuit(), lanes(slot_operand(position)),
                                   lanes(length), false)}), next, done);
        begin(next, span);
        if (!equality && !hashing) {
            const auto nonzero = current().synth.circuit().any(lanes(slot_operand(position)));
            const auto separator = new_block();
            const auto element = new_block();
            branch(bits(boolean, {nonzero}), separator, element);
            begin(separator, span);
            append(',');
            append(' ');
            jump(element);
            begin(element, span);
        }
        process(type.arguments.front(), 0, slot_operand(position));
        const auto incremented = add(current().synth.circuit(), lanes(slot_operand(position)),
                                     constant_word(1, 64), false);
        assign(position, bits(count, incremented.value));
        jump(head);
        begin(done, span);
    }
    const auto return_value = [&](Operand value) {
        ExitSpec exit;
        exit.kind = nvr::ExitKind::Return;
        exit.arguments.push_back(std::move(value));
        close({exit});
    };
    if (equality) {
        return_value(immediate(boolean, 1));
        begin(negative, span);
        return_value(immediate(boolean, 0));
    } else if (hashing) {
        return_value(slot_operand(hash));
    } else {
        append(']');
        return_value(state(nvr::StateOp::ConstructText, text, {buffer}, text, span));
    }
    finish();
}

void FunctionCompiler::compile_hash_text() {
    const auto text = c_.scalar(nvr::TypeKind::Text);
    const auto u8 = c_.scalar(nvr::TypeKind::Unsigned, 8);
    const auto u32 = c_.scalar(nvr::TypeKind::Unsigned, 32);
    const auto u64 = c_.scalar(nvr::TypeKind::Unsigned, 64);
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    const auto source = new_slot(text);
    const auto position = new_slot(u64);
    const auto hash = new_slot(u32);
    function_.parameter_count = 1;
    function_.result_type = u32;
    const Span none;
    begin(new_block(), none);
    const auto length = state(nvr::StateOp::ReadLength, 0, {slot_operand(source)}, u64, none);
    assign(position, immediate(u64, 0));
    assign(hash, immediate(u32, 0));
    const auto head = new_block();
    jump(head);
    begin(head, none);
    const auto more = less(current().synth.circuit(), lanes(slot_operand(position)), lanes(length), false);
    const auto body = new_block();
    const auto done = new_block();
    branch(bits(boolean, {more}), body, done);
    begin(body, none);
    const auto byte = state(nvr::StateOp::ReadElement, 0, {slot_operand(source), slot_operand(position)}, u8, none);
    auto& circuit = current().synth.circuit();
    const auto previous = lanes(slot_operand(hash));
    Word shifted(5, Signal::of(0));
    shifted.insert(shifted.end(), previous.begin(), previous.end() - 5);
    auto padded = lanes(byte);
    padded.resize(32, Signal::of(0));
    // Only the low word is retained. (previous << 5) - previous is 31 * previous.
    auto next = add(circuit, subtract(circuit, shifted, previous, false).value, padded, false);
    assign(hash, bits(u32, std::move(next.value)));
    auto advance = add(circuit, lanes(slot_operand(position)), constant_word(1, 64), false);
    assign(position, bits(u64, std::move(advance.value)));
    jump(head);
    begin(done, none);
    ExitSpec exit;
    exit.kind = nvr::ExitKind::Return;
    exit.arguments = {slot_operand(hash)};
    close({exit});
    finish();
}

void FunctionCompiler::compile_start() {
    // start(args): the initialization sequence, then `return main(args)`,
    // or for a test, the test's call and `return 0`.
    const auto& main = c_.symbol(origin_.symbol);
    const bool test = main.parameters.empty();
    const auto standard = c_.standard_exit_variants(main.type.symbol);
    const auto text_array = c_.type_index({nvr::TypeKind::Array, 0, c_.scalar(nvr::TypeKind::Text)});
    const auto arguments =
        new_slot(test ? text_array : c_.type_of(c_.symbol(main.parameters.front()).type, main.span));
    function_.parameter_count = 1;
    function_.result_type = test || standard ? c_.scalar(nvr::TypeKind::Signed, 32) : c_.type_of(main.type, main.span);
    const auto& items = c_.startup();
    begin(new_block(), items.empty() ? main.span : items.front().node->span);
    for (const auto& item : items) {
        if (!open()) {
            break;
        }
        receiver_module_ = item.module;
        statement(*item.node);
    }
    if (open()) {
        receiver_module_ = no_symbol;
        std::vector<Operand> passed;
        if (!test) {
            passed.push_back(slot_operand(arguments));
        }
        const auto result_type = c_.type_of(main.type, main.span);
        const auto status = invoke({Role::Function, origin_.symbol, origin_.receiver}, std::move(passed),
                                   result_type, main.span);
        if (standard) {
            return_standard_status(status, *standard, main.span);
        } else {
            ExitSpec exit;
            exit.kind = nvr::ExitKind::Return;
            exit.arguments = {test ? immediate(function_.result_type, 0) : status};
            close({exit});
        }
    }
    finish();
}

void FunctionCompiler::return_standard_status(const Operand& value, const std::array<SymbolId, 3>& variants,
                                               const Span& span) {
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    const auto tag_type = c_.scalar(nvr::TypeKind::Unsigned, 32);
    const auto tag = state(nvr::StateOp::ReadField, 0, {value}, tag_type, span);
    const auto matches = [&](SymbolId variant) {
        const auto input = lanes(tag);
        return bits(boolean, {equal(current().synth.circuit(), input, constant_word(c_.ordinal(variant), 32))});
    };
    const auto complete = [&](Operand status) {
        ExitSpec exit;
        exit.kind = nvr::ExitKind::Return;
        exit.arguments = {std::move(status)};
        close({exit});
    };
    const auto ok = new_block();
    const auto not_ok = new_block();
    branch(matches(variants[0]), ok, not_ok);
    begin(ok, span);
    complete(immediate(function_.result_type, 0));
    begin(not_ok, span);
    const auto failure = new_block();
    const auto with_status = new_block();
    branch(matches(variants[1]), failure, with_status);
    begin(failure, span);
    complete(immediate(function_.result_type, 1));
    begin(with_status, span);
    const auto status = state(nvr::StateOp::ReadField, c_.payload_start(variants[2], span),
                              {value}, function_.result_type, span);
    // status < 0, then status > 255: each block compares the status with one
    // constant, as a source comparison does, so the decompiler lifts both.
    const auto invalid = new_block();
    const auto upper = new_block();
    const auto valid = new_block();
    const auto below = less(current().synth.circuit(), lanes(status), constant_word(0, 32), true);
    branch(bits(boolean, {below}), invalid, upper);
    begin(upper, span);
    const auto above = less(current().synth.circuit(), constant_word(255, 32), lanes(status), true);
    branch(bits(boolean, {above}), invalid, valid);
    begin(valid, span);
    complete(status);
    begin(invalid, span);
    ExitSpec trap;
    trap.kind = nvr::ExitKind::MessageTrap;
    const auto text = c_.scalar(nvr::TypeKind::Text);
    trap.arguments = {{Operand::Kind::Constant, text, c_.text_constant("invalid argument: exit status", span), 0, {}}};
    operation_at(span);
    close({trap});
}

void FunctionCompiler::compile() {
    switch (origin_.role) {
    case Role::Function: compile_function(); break;
    case Role::Constructor: compile_constructor(); break;
    case Role::Destructor: compile_destructor(); break;
    case Role::Concatenate: compile_concatenate(); break;
    case Role::HashText: compile_hash_text(); break;
    case Role::ValueEquals: compile_value_member(IrOp::ValueEquals); break;
    case Role::ValueHash: compile_value_member(IrOp::ValueHash); break;
    case Role::ValueText: compile_value_member(IrOp::ValueText); break;
    case Role::Start: compile_start(); break;
    }
}

} // namespace

Conversion convert(const IrProgram& program, const ConversionOptions& options) {
    return Converter(program, options).run();
}

Conversion build(IrProgram program, const BuildOptions& options) {
    // Specialization needs verified IR, and the result is verified again:
    // by the optimizer, or directly when optimization is off.
    require_valid_ir(program, "before specialization");
    specialize(program);
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
