#include "nir_format.hpp"
#include "lexer/lexer.hpp"
#include "attributes.hpp"
#include "hook_arguments.hpp"
#include "host_binding.hpp"
#include "integer.hpp"
#include "law_program.hpp"
#include "verifier.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string_view>

namespace neura {
namespace {

constexpr std::string_view magic = "NEIR";
constexpr std::string_view payload_names_tag = "PNAM";
constexpr std::string_view laws_tag = "LAWS";
constexpr std::string_view nil_summaries_tag = "NILS";
constexpr std::string_view non_nil_uses_tag = "NNIL";
constexpr std::uint32_t absent = 0xffffffffU;
// Bounds the reader's recursion and that of the passes that later walk the
// program, with room to spare under sanitizers.
constexpr std::size_t max_depth = 1024;
// The last operation tag of each minor version.
constexpr std::uint8_t max_op_1_0 = static_cast<std::uint8_t>(IrOp::Discard);
constexpr std::uint8_t max_op_1_1 = static_cast<std::uint8_t>(IrOp::Fail);
constexpr std::uint8_t max_op_1_2 = static_cast<std::uint8_t>(IrOp::SelectValue);
constexpr std::uint8_t max_op_1_3 = static_cast<std::uint8_t>(IrOp::ValueText);
constexpr std::uint8_t max_op_1_4 = static_cast<std::uint8_t>(IrOp::RepeatCollection);
constexpr std::uint8_t max_op_1_7 = static_cast<std::uint8_t>(IrOp::ReferencePlace);
constexpr std::uint8_t max_op_1_9 = static_cast<std::uint8_t>(IrOp::SchedulerCall);
constexpr std::uint8_t max_op_1_12 = static_cast<std::uint8_t>(IrOp::AnyUnpack);
static_assert(static_cast<unsigned>(IrOp::HookRead) == 64 && static_cast<unsigned>(IrOp::HookAdmit) == 65);
static_assert(static_cast<unsigned>(IrOp::AnyPack) == 66 && static_cast<unsigned>(IrOp::AnyTag) == 67 &&
              max_op_1_12 == 68);
constexpr std::uint8_t max_op_records = static_cast<std::uint8_t>(IrOp::ConformanceCall);
static_assert(max_op_records == 69);
constexpr std::uint8_t max_op_1_14 = static_cast<std::uint8_t>(IrOp::Yield);
static_assert(static_cast<unsigned>(IrOp::Cleanup) == 70 && static_cast<unsigned>(IrOp::Pending) == 71 &&
              max_op_1_14 == 72);
constexpr std::uint8_t max_op_expressions = static_cast<std::uint8_t>(IrOp::TypeOf);
static_assert(static_cast<unsigned>(IrOp::ConcatText) == 73 && max_op_expressions == 74);
// NeuraIR 1.9 stores a node's poll in the top bit of its operation byte.
constexpr std::uint8_t poll_bit = 0x80;
constexpr std::uint16_t task_minor = 9;
// NeuraIR 1.10 adds symbol attributes and deferred initialization.
constexpr std::uint16_t attribute_minor = 10;
constexpr std::uint16_t newest_node_minor = nir_expressions_minor;

bool valid_non_nil_use(const IrProgram& program, const Span& use, SymbolId id) {
    if (id >= program.symbols.size() || use.file.empty() || !use.line || !use.column || !use.length ||
        use.offset > std::numeric_limits<std::size_t>::max() - use.length) return false;
    const auto& binding = program.symbols[id];
    return (binding.kind == SymbolKind::Local || binding.kind == SymbolKind::Parameter ||
            (binding.kind == SymbolKind::Global && binding.constant)) &&
           (binding.type.kind == TypeKind::TypeParameter ||
            (binding.type.kind == TypeKind::Object && !binding.type.name.starts_with('&')));
}

bool valid_nil_binding(const IrProgram& program, const IrNilBinding& record) {
    if (record.binding >= program.symbols.size() || record.initializer.file.empty()) return false;
    const auto& symbol = program.symbols[record.binding];
    return (symbol.kind == SymbolKind::Field || symbol.kind == SymbolKind::Global) &&
        (symbol.type.kind == TypeKind::Object || symbol.type.kind == TypeKind::TypeParameter);
}

bool valid_nil_result(const IrProgram& program, const IrNilResult& record) {
    if (record.function >= program.symbols.size()) return false;
    const auto& symbol = program.symbols[record.function];
    return symbol.kind == SymbolKind::Function && !symbol.external && symbol.has_body &&
        (symbol.type.kind == TypeKind::Object || symbol.type.kind == TypeKind::TypeParameter) &&
        (record.state == NilState::MaybeNil ? !record.nil_return.file.empty() : record.nil_return.file.empty());
}

bool hook_attribute(const IrSymbol& symbol) {
    return has_attribute(symbol, AttributeId::HookGet) || has_attribute(symbol, AttributeId::HookSet) ||
           has_attribute(symbol, AttributeId::HookOf);
}

bool may_poll(IrOp op) {
    return op == IrOp::Loop || op == IrOp::Call || op == IrOp::VirtualCall || op == IrOp::CallClosure;
}

std::uint16_t type_minor(const IrType& type) {
    if (type.kind == TypeKind::Any) return nir_any_minor;
    if (type.kind == TypeKind::Object && type.name.starts_with('&')) return 7;
    if (is_function_value(type) || (type.kind == TypeKind::Enum && !type.arguments.empty())) return 3;
    return type.kind == TypeKind::Tuple ? 2 : 0;
}

std::uint16_t required_minor(const IrType& type) {
    auto version = type_minor(type);
    for (const auto& argument : type.arguments) {
        version = std::max(version, required_minor(argument));
    }
    return version;
}

bool new_operator(const IrNode& node) {
    return (node.op == IrOp::Unary && node.text == "~") ||
           (node.op == IrOp::Binary && is_bitwise_binary(node.text));
}

bool scoped_statements(const IrNode& node) {
    return node.op == IrOp::ScopedValue && !node.body.empty() &&
        std::any_of(node.body.begin(), node.body.end() - 1,
                    [](const IrNode& prefix) { return prefix.op != IrOp::Initialize; });
}

// A return anywhere inside a scoped value (NeuraIR 1.12).
bool contains_return(const IrNode& node) {
    if (node.op == IrOp::Return) return true;
    for (const auto* region : {&node.operands, &node.body, &node.alternative})
        for (const auto& child : *region)
            if (contains_return(child)) return true;
    return false;
}

bool scoped_return(const IrNode& node) {
    return node.op == IrOp::ScopedValue && contains_return(node);
}

bool mentions_any(const IrType& type) {
    return type.kind == TypeKind::Any || std::ranges::any_of(type.arguments, mentions_any);
}

bool needs_any_minor(const std::vector<IrNode>& nodes) {
    for (const auto& node : nodes) {
        if (scoped_return(node) || (node.op >= IrOp::AnyPack && node.op <= IrOp::AnyUnpack) || mentions_any(node.type) ||
            std::ranges::any_of(node.type_arguments, mentions_any))
            return true;
        for (const auto* region : {&node.operands, &node.body, &node.alternative})
            if (needs_any_minor(*region)) return true;
    }
    return false;
}

// A Cleanup, Pending or Yield anywhere (NeuraIR 1.14).
bool needs_lifetime_minor(const std::vector<IrNode>& nodes) {
    for (const auto& node : nodes) {
        if (static_cast<std::uint8_t>(node.op) > max_op_records) return true;
        for (const auto* region : {&node.operands, &node.body, &node.alternative})
            if (needs_lifetime_minor(*region)) return true;
    }
    return false;
}

bool class_qualified_load(const IrNode& node) {
    return node.op == IrOp::Load && !node.operands.empty() &&
        node.operands.front().type.kind != TypeKind::Module;
}

std::uint16_t required_minor(const std::vector<IrNode>& nodes) {
    std::uint16_t version = 0;
    for (const auto& node : nodes) {
        const auto op = static_cast<std::uint8_t>(node.op);
        if (node.op == IrOp::ConcatText || node.op == IrOp::TypeOf) return nir_expressions_minor;
        if (node.op == IrOp::ConformanceCall) version = std::max(version, nir_records_minor);
        if (is_deferred_initialize(node)) {
            version = std::max(version, attribute_minor);
        } else if (op > max_op_1_7 || node.poll) {
            version = std::max(version, task_minor);
        } else if (op > max_op_1_4) {
            version = std::max<std::uint16_t>(version, 7);
        } else if (op > max_op_1_3 || scoped_statements(node) || class_qualified_load(node)) {
            version = std::max<std::uint16_t>(version, 4);
        } else if (op > max_op_1_2) {
            version = std::max<std::uint16_t>(version, 3);
        } else if (op > max_op_1_1 || new_operator(node)) {
            version = std::max<std::uint16_t>(version, 2);
        } else if (op > max_op_1_0 || (node.op == IrOp::Loop && !node.alternative.empty())) {
            version = std::max<std::uint16_t>(version, 1);
        }
        version = std::max(version, required_minor(node.type));
        for (const auto& argument : node.type_arguments) {
            version = std::max(version, required_minor(argument));
        }
        for (const auto* region : {&node.operands, &node.body, &node.alternative}) {
            version = std::max(version, required_minor(*region));
        }
        if (version == newest_node_minor) return version;
    }
    return version;
}

enum Flag : std::uint8_t {
    Exported = 1, Constant = 2, External = 4, HasBody = 8, HasPayload = 16, HostBound = 32, LibraryFile = 64,
    HasInterfacesOrBounds = 128
};

enum LinkFlag : std::uint8_t {
    VariadicMask = 3, UserCast = 4, OverloadBranch = 8, Singleton = 16,
    // NeuraIR 1.10: a nonempty attribute vector follows the references.
    HasAttributes = 32
};

// A program may load a standard library with named variants even when no
// user declaration exposes them and no value member can observe their names.
// Keep such programs at their earlier format minor.
bool needs_payload_names(const IrProgram& program) {
    const auto& symbols = program.symbols;
    std::vector<std::vector<SymbolId>> children(symbols.size());
    for (SymbolId id = 0; id < symbols.size(); ++id)
        if (symbols[id].owner < symbols.size()) children[symbols[id].owner].push_back(id);

    std::set<SymbolId> exposed;
    const auto exposes = [&](const auto& self, const IrType& type) -> bool {
        for (const auto& argument : type.arguments)
            if (self(self, argument)) return true;
        const auto id = type.symbol;
        if (id >= symbols.size() || !exposed.insert(id).second) return false;
        const auto& declaration = symbols[id];
        if (self(self, declaration.type)) return true;
        if (declaration.base < symbols.size() && self(self, symbols[declaration.base].type)) return true;
        for (const auto& view : declaration.interfaces)
            if (self(self, view)) return true;
        if (declaration.bound && self(self, *declaration.bound)) return true;
        for (const auto& bound : declaration.additional_bounds)
            if (self(self, bound)) return true;
        for (const auto child : children[id]) {
            const auto& member = symbols[child];
            if (member.kind == SymbolKind::Variant) {
                if (!member.payload_names.empty()) return true;
                for (const auto& payload : member.payload_types)
                    if (self(self, payload)) return true;
            } else if (member.exported || member.kind == SymbolKind::Field) {
                if (self(self, member.type)) return true;
                for (const auto parameter : member.parameters)
                    if (parameter < symbols.size() && self(self, symbols[parameter].type)) return true;
            }
        }
        return false;
    };
    // Only declarations reachable from the user's exported interface are
    // signature roots. Imported trusted modules contribute when exposed.
    for (SymbolId file = 0; file < symbols.size(); ++file) {
        if (symbols[file].kind != SymbolKind::File || !symbols[file].library_path.empty()) continue;
        for (const auto child : children[file]) {
            const auto& item = symbols[child];
            // A top-level module can be the root of a file import even
            // without an export modifier; its members still need export.
            if (!item.exported && item.kind != SymbolKind::Module) continue;
            if (exposes(exposes, item.type)) return true;
            for (const auto parameter : item.parameters)
                if (parameter < symbols.size() && exposes(exposes, symbols[parameter].type)) return true;
        }
    }
    // A generic function can format its type parameter in a different
    // module. Follow calls back from each value-text operation to preserve
    // names of both explicit and inferred concrete instantiations.
    std::set<SymbolId> printers;
    std::vector<std::pair<SymbolId, const IrNode*>> calls;
    const auto visit = [&](const auto& self, SymbolId owner, const IrNode& node) -> bool {
        if (node.op == IrOp::ValueText && !node.operands.empty()) {
            printers.insert(owner);
            if (exposes(exposes, node.operands.front().type)) return true;
        }
        if (node.symbol < symbols.size() && symbols[node.symbol].kind == SymbolKind::Function)
            calls.emplace_back(owner, &node);
        for (const auto* region : {&node.operands, &node.body, &node.alternative})
            for (const auto& child : *region)
                if (self(self, owner, child)) return true;
        return false;
    };
    for (const auto& body : program.bodies)
        for (const auto& node : body.instructions)
            if (visit(visit, body.owner, node)) return true;
    for (std::size_t previous = 0; previous != printers.size();) {
        previous = printers.size();
        for (const auto& [caller, call] : calls) {
            if (!printers.contains(call->symbol)) continue;
            printers.insert(caller);
            for (const auto& argument : call->type_arguments)
                if (exposes(exposes, argument)) return true;
            if (!call->operands.empty())
                for (const auto& argument : call->operands.front().type.arguments)
                    if (exposes(exposes, argument)) return true;
        }
    }
    return false;
}

class Writer {
public:
    explicit Writer(const IrProgram& program) {
        // Span paths form a table in order of first use.
        for (const auto& symbol : program.symbols) {
            path(symbol.span.file);
            for_each_attribute_span(symbol.attributes, [&](const Span& span) { path(span.file); });
        }
        for (const auto& body : program.bodies) {
            for (const auto& node : body.instructions) {
                paths(node);
            }
        }
        for (const auto& constant : program.static_constants) paths(constant.value);
        for (const auto& record : program.nil_bindings) path(record.initializer.file);
        for (const auto& record : program.nil_results) path(record.nil_return.file);
        for (const auto& use : program.non_nil_uses) path(use.first.file);
        for (const auto& law : program.laws) {
            law_paths(law.statement);
            if (law.certificate) law_paths(*law.certificate);
            for (const auto& abbreviation : law.abbreviations) law_paths(abbreviation.value);
        }
    }

    std::vector<std::uint8_t> write(const IrProgram& program) {
        for (char c : magic) {
            u8(static_cast<std::uint8_t>(c));
        }
        u16(nir_major_version);
        std::uint16_t version = program.static_constants.empty() ? 0 : 6;
        if (program.native_library) version = 8;
        std::size_t named_variants = 0;
        for (const auto& item : program.symbols) {
            if (!item.payload_names.empty()) ++named_variants;
            if (item.variadic != VariadicKind::None || item.user_cast ||
                item.overload_group != no_symbol || item.singleton_instance != no_symbol) {
                version = std::max<std::uint16_t>(version, 6);
            }
            if (is_static_initializer(item)) version = std::max<std::uint16_t>(version, 5);
            if (is_static_scope(item)) version = std::max<std::uint16_t>(version, 4);
            if (host_callback(item)) version = std::max<std::uint16_t>(version, 8);
            if (!item.attributes.empty()) version = std::max(version, attribute_minor);
            if (hook_attribute(item)) version = std::max(version, nir_hooks_minor);
            if (item.host_binding || !item.library_path.empty() || item.kind == SymbolKind::Interface ||
                !item.interfaces.empty() || !item.additional_bounds.empty()) {
                version = std::max<std::uint16_t>(version, 3);
            }
            version = std::max(version, required_minor(item.type));
            if (item.bound) version = std::max(version, required_minor(*item.bound));
            if (!item.payload_types.empty()) version = std::max<std::uint16_t>(version, 2);
            for (const auto& payload : item.payload_types) {
                version = std::max(version, required_minor(payload));
            }
        }
        for (const auto& body : program.bodies) {
            if (version >= newest_node_minor) break;
            version = std::max(version, required_minor(body.instructions));
        }
        if (std::ranges::any_of(program.bodies, [](const IrBody& body) { return needs_any_minor(body.instructions); }) ||
            std::ranges::any_of(program.symbols, [](const IrSymbol& item) {
                return mentions_any(item.type) || std::ranges::any_of(item.payload_types, mentions_any) ||
                       (item.bound && mentions_any(*item.bound));
            }))
            version = std::max(version, nir_any_minor);
        if (named_variants != 0 && version < nir_records_minor) {
            if (needs_payload_names(program)) version = nir_records_minor;
            else named_variants = 0;
        }
        if (std::ranges::any_of(program.bodies, [](const IrBody& body) { return needs_lifetime_minor(body.instructions); }) ||
            std::ranges::any_of(program.symbols, [](const IrSymbol& item) {
                return std::ranges::any_of(item.attributes, [](const IrAttribute& attribute) {
                    return attribute.id == AttributeId::GeneratorResume;
                });
            }))
            version = std::max(version, nir_lifetime_minor);
        if (!program.laws.empty()) version = nir_laws_minor;
        if (!program.non_nil_uses.empty() || !program.nil_bindings.empty() || !program.nil_results.empty())
            version = nir_narrowing_minor;
        polls_ = version >= 9;
        u16(version);
        count(files_.size());
        for (const auto& file : files_) {
            text(file);
        }
        count(program.symbols.size());
        for (const auto& item : program.symbols) {
            symbol(item, version);
        }
        count(program.bodies.size());
        for (const auto& body : program.bodies) {
            reference(body.owner);
            nodes(body.instructions);
        }
        if (version >= 6) {
            count(program.static_constants.size());
            for (const auto& constant : program.static_constants) {
                reference(constant.field);
                node(constant.value);
            }
        }
        if (version >= 8) {
            u8(program.native_library ? 1 : 0);
            if (program.native_library) {
                text(program.native_library->triple);
                count(program.native_library->object.size());
                for (const auto byte : program.native_library->object) u8(byte);
                count(program.native_library->symbols.size());
                for (const auto& symbol : program.native_library->symbols) text(symbol);
            }
        }
        if (named_variants != 0) {
            for (const char letter : payload_names_tag) u8(letter);
            count(named_variants);
            for (SymbolId id = 0; id < program.symbols.size(); ++id) {
                const auto& item = program.symbols[id];
                if (item.payload_names.empty()) continue;
                if (item.kind != SymbolKind::Variant || item.payload_names.size() != item.payload_types.size())
                    throw NirError("named payload fields require one name per variant payload");
                reference(id);
                count(item.payload_names.size());
                std::set<std::string_view> seen;
                for (const auto& name : item.payload_names) {
                    if (!is_identifier(name) || !seen.insert(name).second)
                        throw NirError("invalid or repeated named payload field");
                    text(name);
                }
            }
        }
        if (!program.laws.empty()) {
            for (const char letter : laws_tag) u8(letter);
            count(program.laws.size());
            for (const auto& law : program.laws) {
                reference(law.law_symbol);
                reference(law.owner);
                u8(static_cast<std::uint8_t>(law.status));
                u32(law.introductions);
                term(law.statement);
                references(law.footprint);
                count(law.abbreviations.size());
                for (const auto& abbreviation : law.abbreviations) {
                    text(abbreviation.name);
                    term(abbreviation.value);
                }
                u8(law.certificate.has_value());
                if (law.certificate) proof(*law.certificate);
            }
        }
        if (!program.nil_bindings.empty() || !program.nil_results.empty()) {
            for (const char letter : nil_summaries_tag) u8(letter);
            count(program.nil_bindings.size());
            for (const auto& record : program.nil_bindings) {
                if (!valid_nil_binding(program, record)) throw NirError("invalid nil binding contract");
                reference(record.binding);
                u8(record.initialized_nil);
                span(record.initializer);
            }
            count(program.nil_results.size());
            for (const auto& record : program.nil_results) {
                if (!valid_nil_result(program, record)) throw NirError("invalid nil result summary");
                reference(record.function);
                u8(static_cast<std::uint8_t>(record.state));
                span(record.nil_return);
            }
        }
        if (!program.non_nil_uses.empty()) {
            for (const char letter : non_nil_uses_tag) u8(letter);
            count(program.non_nil_uses.size());
            for (const auto& [use, binding] : program.non_nil_uses) {
                if (!valid_non_nil_use(program, use, binding)) throw NirError("invalid nil-narrowing fact");
                span(use);
                reference(binding);
            }
        }
        return std::move(bytes_);
    }

private:
    void path(const std::string& file) {
        if (!file.empty() && indices_.emplace(file, static_cast<std::uint32_t>(files_.size())).second) {
            files_.push_back(file);
        }
    }

    void paths(const IrNode& node) {
        path(node.span.file);
        for (const auto* region : {&node.operands, &node.body, &node.alternative}) {
            for (const auto& child : *region) {
                paths(child);
            }
        }
    }

    template<class Tree>
    void law_paths(const Tree& tree) {
        path(tree.span.file);
        for (const auto& child : tree.terms) law_paths(child);
        if constexpr (requires { tree.proofs; })
            for (const auto& child : tree.proofs) law_paths(child);
    }

    void term(const LawTerm& value) {
        u8(static_cast<std::uint8_t>(value.kind));
        span(value.span);
        type(value.type);
        reference(value.symbol);
        reference(value.receiver);
        u32(value.index);
        terms(value.terms);
    }

    void terms(const std::vector<LawTerm>& values) {
        count(values.size());
        for (const auto& value : values) term(value);
    }

    void proof(const LawProof& value) {
        u8(static_cast<std::uint8_t>(value.kind));
        span(value.span);
        reference(value.symbol);
        reference(value.receiver);
        u32(value.index);
        count(value.bindings.size());
        for (const auto binding : value.bindings) u32(binding);
        terms(value.terms);
        count(value.proofs.size());
        for (const auto& child : value.proofs) proof(child);
    }

    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(std::uint16_t value) { little_endian(value, 2); }
    void u32(std::uint32_t value) { little_endian(value, 4); }
    void u64(std::uint64_t value) { little_endian(value, 8); }

    void little_endian(std::uint64_t value, int width) {
        for (int i = 0; i < width; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }

    void count(std::size_t value) {
        if (value >= absent) {
            throw NirError("NeuraIR vector has too many elements for .nir");
        }
        u32(static_cast<std::uint32_t>(value));
    }

    void reference(SymbolId id) {
        if (id == no_symbol) {
            u32(absent);
            return;
        }
        if (id >= absent) {
            throw NirError("symbol index is too large for .nir");
        }
        u32(static_cast<std::uint32_t>(id));
    }

    void text(const std::string& value) {
        count(value.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void span(const Span& value) {
        u32(value.file.empty() ? absent : indices_.at(value.file));
        u64(value.line);
        u64(value.column);
        u64(value.offset);
        u64(value.length);
    }

    void type(const IrType& value) {
        u8(static_cast<std::uint8_t>(value.kind));
        text(value.name);
        reference(value.symbol);
        types(value.arguments);
    }

    void types(const std::vector<IrType>& values) {
        count(values.size());
        for (const auto& value : values) {
            type(value);
        }
    }

    void references(const std::vector<SymbolId>& values) {
        count(values.size());
        for (auto value : values) {
            reference(value);
        }
    }

    void symbol(const IrSymbol& value, std::uint16_t version) {
        u8(static_cast<std::uint8_t>(value.kind));
        text(value.name);
        span(value.span);
        reference(value.owner);
        reference(value.base);
        reference(value.overrides);
        type(value.type);
        references(value.parameters);
        references(value.generics);
        u8(value.bound ? 1 : 0);
        if (value.bound) {
            type(*value.bound);
        }
        const bool extra_types = !value.interfaces.empty() || !value.additional_bounds.empty();
        u8(static_cast<std::uint8_t>((value.exported ? Exported : 0) | (value.constant ? Constant : 0) |
                                     (value.external ? External : 0) | (value.has_body ? HasBody : 0) |
                                     (value.payload_types.empty() ? 0 : HasPayload) |
                                     (value.host_binding ? HostBound : 0) |
                                     (value.library_path.empty() ? 0 : LibraryFile) |
                                     (extra_types ? HasInterfacesOrBounds : 0)));
        if (!value.payload_types.empty()) types(value.payload_types);
        if (value.host_binding) {
            text(value.host_binding->name);
            u32(value.host_binding->version);
        }
        if (!value.library_path.empty()) text(value.library_path);
        if (extra_types) {
            types(value.interfaces);
            types(value.additional_bounds);
        }
        if (version >= 6) {
            u8(static_cast<std::uint8_t>(static_cast<std::uint8_t>(value.variadic) |
                (value.user_cast ? UserCast : 0) |
                (value.overload_group != no_symbol ? OverloadBranch : 0) |
                (value.singleton_instance != no_symbol ? Singleton : 0) |
                (value.attributes.empty() ? 0 : HasAttributes)));
            if (value.overload_group != no_symbol) reference(value.overload_group);
            if (value.singleton_instance != no_symbol) reference(value.singleton_instance);
            if (!value.attributes.empty()) {
                count(value.attributes.size());
                for (const auto& item : value.attributes) {
                    u16(static_cast<std::uint16_t>(item.id));
                    attribute_value(item.value);
                }
            }
        }
    }

    void attribute_value(const IrAttributeValue& value) {
        u8(static_cast<std::uint8_t>(value.kind));
        switch (value.kind) {
        case AttributeValueKind::Bool: u8(value.integer != 0 ? 1 : 0); break;
        case AttributeValueKind::Signed:
        case AttributeValueKind::Unsigned: u64(value.integer); break;
        case AttributeValueKind::Text: text(value.text); break;
        case AttributeValueKind::Symbol: reference(value.symbol); break;
        case AttributeValueKind::Type: type(value.type); break;
        case AttributeValueKind::Span: span(value.span); break;
        case AttributeValueKind::List:
            count(value.items.size());
            for (const auto& item : value.items) attribute_value(item);
            break;
        }
    }

    void nodes(const std::vector<IrNode>& values) {
        count(values.size());
        for (const auto& value : values) {
            node(value);
        }
    }

    void node(const IrNode& value) {
        // Events are derived on the linked program and never stored.
        if (value.op == IrOp::HookRead || value.op == IrOp::HookAdmit)
            throw NirError("class hook events exist only in an elaborated program and are not written to .nir");
        if (static_cast<std::uint8_t>(value.op) > max_op_expressions)
            throw NirError("invalid operation tag " + std::to_string(static_cast<unsigned>(value.op)));
        if (linked_argument_annotation(value)) throw NirError("argument transport is derived after linking");
        // Only bodies carry polls, and a body with one makes the file 1.9.
        u8(static_cast<std::uint8_t>(value.op) | (polls_ && value.poll ? poll_bit : 0));
        type(value.type);
        span(value.span);
        reference(value.symbol);
        text(value.text);
        nodes(value.operands);
        nodes(value.body);
        nodes(value.alternative);
        types(value.type_arguments);
    }

    std::vector<std::string> files_;
    std::map<std::string, std::uint32_t> indices_;
    std::vector<std::uint8_t> bytes_;
    bool polls_ = false;
};

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, std::string origin) : bytes_(bytes), origin_(std::move(origin)) {}

    IrProgram read(std::uint16_t* minor_version) {
        if (!is_nir(bytes_)) {
            fail("not a NeuraIR file");
        }
        position_ = magic.size();
        const auto major = u16();
        minor_ = u16();
        if (major != nir_major_version || minor_ > nir_minor_version) {
            fail("NeuraIR format " + std::to_string(major) + "." + std::to_string(minor_) +
                 " is not supported; this reader accepts 1.0 through 1." + std::to_string(nir_minor_version));
        }
        // 1.11 and 1.12 belong to milestones merged beside this one and add
        // no operation here.
        constexpr std::uint8_t operation_limits[]{
            max_op_1_0, max_op_1_1, max_op_1_2, max_op_1_3, max_op_1_4, max_op_1_4, max_op_1_4, max_op_1_7,
            max_op_1_7, max_op_1_9, max_op_1_9, max_op_1_9, max_op_1_12, max_op_records, max_op_1_14,
            max_op_expressions, max_op_expressions};
        static_assert(std::size(operation_limits) == nir_minor_version + 1);
        max_op_ = operation_limits[minor_];
        latches_ = minor_ > 0;
        aggregates_ = minor_ >= 2;
        const auto file_count = count(4);
        files_.reserve(file_count);
        for (std::uint32_t i = 0; i < file_count; ++i) {
            files_.push_back(text());
        }
        IrProgram program;
        symbol_count_ = count(1);
        program.symbols.reserve(symbol_count_);
        for (std::uint32_t i = 0; i < symbol_count_; ++i) {
            program.symbols.push_back(symbol());
        }
        const auto body_count = count(8);
        program.bodies.reserve(body_count);
        for (std::uint32_t i = 0; i < body_count; ++i) {
            IrBody body;
            body.owner = required_reference();
            body.instructions = nodes(0);
            program.bodies.push_back(std::move(body));
        }
        if (minor_ >= 6) {
            const auto constants = count(5);
            program.static_constants.reserve(constants);
            for (std::uint32_t i = 0; i < constants; ++i)
                program.static_constants.push_back({required_reference(), node(0)});
        }
        if (minor_ >= 8 && u8() != 0) {
            IrNativeLibrary library;
            library.triple = text();
            const auto size = count(1);
            library.object.reserve(size);
            for (std::uint32_t i = 0; i < size; ++i) library.object.push_back(u8());
            const auto symbols = count(4);
            for (std::uint32_t i = 0; i < symbols; ++i) library.symbols.push_back(text());
            if (library.triple.empty() || library.object.empty()) fail("a native library needs a triple and an object");
            program.native_library = std::move(library);
        }
        if (remaining() >= payload_names_tag.size() &&
            std::equal(payload_names_tag.begin(), payload_names_tag.end(), bytes_.begin() + position_)) {
            if (minor_ < nir_records_minor) fail("named payload fields require NeuraIR format 1.13");
            position_ += payload_names_tag.size();
            const auto entries = count(8);
            if (entries == 0 || entries > symbol_count_) fail("invalid named variant count");
            for (std::uint32_t i = 0; i < entries; ++i) {
                auto& item = program.symbols[required_reference()];
                if (item.kind != SymbolKind::Variant || !item.payload_names.empty())
                    fail("invalid or repeated named variant");
                const auto fields = count(4);
                if (fields == 0 || fields != item.payload_types.size())
                    fail("named payload fields require one name per variant payload");
                std::set<std::string> seen;
                for (std::uint32_t f = 0; f < fields; ++f) {
                    auto name = text();
                    if (!is_identifier(name) || !seen.insert(name).second)
                        fail("invalid or repeated named payload field");
                    item.payload_names.push_back(std::move(name));
                }
            }
        }
        if (remaining() >= laws_tag.size() &&
            std::equal(laws_tag.begin(), laws_tag.end(), bytes_.begin() + position_)) {
            if (minor_ < nir_laws_minor) fail("law records require NeuraIR format 1.16");
            position_ += laws_tag.size();
            program.laws = laws();
        } else if (std::ranges::any_of(program.symbols, [](const IrSymbol& value) {
            return value.kind == SymbolKind::Law;
        })) {
            fail("missing NeuraIR 1.16 law table");
        }
        if (remaining() >= nil_summaries_tag.size() &&
            std::equal(nil_summaries_tag.begin(), nil_summaries_tag.end(), bytes_.begin() + position_)) {
            if (minor_ < nir_narrowing_minor) fail("nil summaries require NeuraIR format 1.16");
            position_ += nil_summaries_tag.size();
            const auto bindings = count(41);
            std::set<SymbolId> seen;
            for (std::uint32_t i = 0; i < bindings; ++i) {
                IrNilBinding record{required_reference(), tag(1, "nil binding contract") != 0, span()};
                if (!valid_nil_binding(program, record) || !seen.insert(record.binding).second)
                    fail("invalid or repeated nil binding contract");
                program.nil_bindings.push_back(std::move(record));
            }
            const auto results = count(41);
            if (bindings == 0 && results == 0) fail("empty nil summary table");
            seen.clear();
            for (std::uint32_t i = 0; i < results; ++i) {
                const auto function = required_reference();
                const auto state = static_cast<NilState>(tag(static_cast<std::uint8_t>(NilState::Unknown), "nil result"));
                IrNilResult record{function, state, span()};
                if (!valid_nil_result(program, record) || !seen.insert(record.function).second)
                    fail("invalid or repeated nil result summary");
                program.nil_results.push_back(std::move(record));
            }
        }
        if (remaining() >= non_nil_uses_tag.size() &&
            std::equal(non_nil_uses_tag.begin(), non_nil_uses_tag.end(), bytes_.begin() + position_)) {
            if (minor_ < nir_narrowing_minor) fail("nil-narrowing facts require NeuraIR format 1.16");
            position_ += non_nil_uses_tag.size();
            const auto entries = count(40);
            if (entries == 0) fail("empty nil-narrowing table");
            program.non_nil_uses.reserve(entries);
            for (std::uint32_t i = 0; i < entries; ++i) {
                auto use = span();
                const auto binding = required_reference();
                if (!valid_non_nil_use(program, use, binding)) fail("invalid nil-narrowing fact");
                program.non_nil_uses.emplace_back(std::move(use), binding);
            }
        }
        if (position_ != bytes_.size()) {
            fail(std::to_string(bytes_.size() - position_) + " unread bytes at the end of the file");
        }
        if (minor_version) *minor_version = minor_;
        return program;
    }

private:
    std::vector<IrLawRecord> laws() {
        const auto entries = count(18);
        if (entries == 0) fail("empty law table");
        std::vector<IrLawRecord> result;
        result.reserve(entries);
        for (std::uint32_t i = 0; i < entries; ++i) {
            IrLawRecord law;
            law.law_symbol = required_reference();
            law.owner = required_reference();
            law.status = static_cast<LawStatus>(tag(static_cast<std::uint8_t>(LawStatus::TrustedAxiom), "law status"));
            law.introductions = u32();
            law.statement = term(0);
            const auto unfolded = count(4);
            law.footprint.reserve(unfolded);
            for (std::uint32_t j = 0; j < unfolded; ++j) law.footprint.push_back(required_reference());
            const auto abbreviations = count(5);
            law.abbreviations.reserve(abbreviations);
            for (std::uint32_t j = 0; j < abbreviations; ++j) {
                auto name = text();
                if (!is_identifier(name)) fail("invalid law abbreviation name");
                law.abbreviations.push_back({std::move(name), term(0)});
            }
            if (tag(1, "law certificate flag")) law.certificate = proof(0);
            result.push_back(std::move(law));
        }
        return result;
    }

    LawTerm term(std::size_t depth) {
        enter(depth);
        LawTerm value;
        value.kind = static_cast<LawTermKind>(tag(static_cast<std::uint8_t>(LawTermKind::Construct), "law term"));
        value.span = span();
        value.type = type(depth + 1);
        value.symbol = reference();
        value.receiver = reference();
        value.index = u32();
        value.terms = terms(depth + 1);
        return value;
    }

    std::vector<LawTerm> terms(std::size_t depth) {
        const auto size = count(1);
        std::vector<LawTerm> result;
        result.reserve(size);
        for (std::uint32_t i = 0; i < size; ++i) result.push_back(term(depth));
        return result;
    }

    LawProof proof(std::size_t depth) {
        enter(depth);
        LawProof value;
        value.kind = static_cast<LawProofKind>(tag(static_cast<std::uint8_t>(LawProofKind::Local), "law proof"));
        value.span = span();
        value.symbol = reference();
        value.receiver = reference();
        value.index = u32();
        const auto bindings = count(4);
        value.bindings.reserve(bindings);
        for (std::uint32_t i = 0; i < bindings; ++i) value.bindings.push_back(u32());
        value.terms = terms(depth + 1);
        const auto proofs = count(1);
        value.proofs.reserve(proofs);
        for (std::uint32_t i = 0; i < proofs; ++i) value.proofs.push_back(proof(depth + 1));
        return value;
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw NirError(origin_ + ": " + message);
    }

    std::size_t remaining() const { return bytes_.size() - position_; }

    std::uint64_t little_endian(std::size_t width) {
        if (remaining() < width) {
            fail("truncated NeuraIR file");
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < width; ++i) {
            value |= std::uint64_t{bytes_[position_ + i]} << (8 * i);
        }
        position_ += width;
        return value;
    }

    std::uint8_t u8() { return static_cast<std::uint8_t>(little_endian(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(little_endian(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(little_endian(4)); }
    std::uint64_t u64() { return little_endian(8); }

    std::size_t size() {
        const auto value = u64();
        if (value > std::numeric_limits<std::size_t>::max()) {
            fail("span field does not fit this host");
        }
        return static_cast<std::size_t>(value);
    }

    std::uint32_t count(std::size_t minimum_element_size) {
        const auto value = u32();
        if (value == absent || value > remaining() / minimum_element_size) {
            fail("truncated NeuraIR vector");
        }
        return value;
    }

    std::string text() {
        const auto length = count(1);
        std::string value(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                          bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + length));
        position_ += length;
        return value;
    }

    SymbolId reference() {
        const auto value = u32();
        if (value == absent) {
            return no_symbol;
        }
        if (value >= symbol_count_) {
            fail("symbol reference " + std::to_string(value) + " is out of range");
        }
        return value;
    }

    SymbolId required_reference() {
        const auto value = reference();
        if (value == no_symbol) {
            fail("missing symbol reference");
        }
        return value;
    }

    std::uint8_t tag(std::uint8_t maximum, const char* what) {
        const auto value = u8();
        if (value > maximum) {
            fail(std::string("invalid ") + what + " tag " + std::to_string(value));
        }
        return value;
    }

    Span span() {
        Span value;
        const auto file = u32();
        if (file != absent) {
            if (file >= files_.size()) {
                fail("source file reference " + std::to_string(file) + " is out of range");
            }
            value.file = files_[file];
        }
        value.line = size();
        value.column = size();
        value.offset = size();
        value.length = size();
        return value;
    }

    void enter(std::size_t depth) const {
        if (depth > max_depth) {
            fail("NeuraIR nesting exceeds " + std::to_string(max_depth) + " levels");
        }
    }

    IrType type(std::size_t depth) {
        enter(depth);
        IrType value;
        const auto max_kind = minor_ >= nir_any_minor ? TypeKind::Any : aggregates_ ? TypeKind::Tuple : TypeKind::Callable;
        value.kind = static_cast<TypeKind>(tag(static_cast<std::uint8_t>(max_kind), "type kind"));
        value.name = text();
        value.symbol = reference();
        value.arguments = types(depth + 1);
        if (type_minor(value) > minor_) fail("type requires a newer NeuraIR format");
        return value;
    }

    std::vector<IrType> types(std::size_t depth) {
        const auto n = count(9);
        std::vector<IrType> values;
        values.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            values.push_back(type(depth));
        }
        return values;
    }

    std::vector<SymbolId> references() {
        const auto n = count(4);
        std::vector<SymbolId> values;
        values.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            values.push_back(required_reference());
        }
        return values;
    }

    IrSymbol symbol() {
        IrSymbol value{};
        const auto max_kind = minor_ >= nir_laws_minor ? SymbolKind::Law
            : minor_ >= 3 ? SymbolKind::Interface : SymbolKind::Variant;
        value.kind = static_cast<SymbolKind>(tag(static_cast<std::uint8_t>(max_kind), "symbol kind"));
        value.name = text();
        if (minor_ < 4 && is_static_scope(value)) fail("class static scopes require NeuraIR format 1.4");
        if (minor_ < 5 && is_static_initializer(value)) fail("class initializers require NeuraIR format 1.5");
        value.span = span();
        // References may point forward, so they are checked against the
        // symbol count rather than the symbols read so far.
        value.owner = reference();
        value.base = reference();
        value.overrides = reference();
        value.type = type(0);
        value.parameters = references();
        value.generics = references();
        const auto has_bound = u8();
        if (has_bound > 1) {
            fail("invalid bound flag");
        }
        if (has_bound == 1) {
            value.bound = type(0);
        }
        const auto flags = u8();
        const auto allowed = Exported | Constant | External | HasBody | (aggregates_ ? HasPayload : 0) |
                             (minor_ >= 3 ? HostBound | LibraryFile | HasInterfacesOrBounds : 0);
        if ((flags & ~allowed) != 0) {
            fail("invalid symbol flags");
        }
        value.exported = (flags & Exported) != 0;
        value.constant = (flags & Constant) != 0;
        value.external = (flags & External) != 0;
        value.has_body = (flags & HasBody) != 0;
        if ((flags & HasPayload) != 0) {
            value.payload_types = types(0);
            if (value.payload_types.empty()) fail("a payload flag requires at least one payload type");
        }
        if ((flags & HostBound) != 0) {
            value.host_binding = IrHostBinding{text(), u32()};
            // NIR 1.8: a host binding on a function with a body is a callback.
            if (value.has_body && minor_ < 8) fail("a host callback needs NIR 1.8");
        }
        if ((flags & LibraryFile) != 0) {
            value.library_path = text();
            if (value.library_path.empty()) fail("a library file needs a nonempty identity");
        }
        if ((flags & HasInterfacesOrBounds) != 0) {
            value.interfaces = types(0);
            value.additional_bounds = types(0);
            if (value.interfaces.empty() && value.additional_bounds.empty())
                fail("an interface/bounds flag needs at least one type");
        }
        if (minor_ >= 6) {
            const auto linkage = u8();
            if ((linkage & HasAttributes) != 0 && minor_ < attribute_minor)
                fail("symbol attributes require NeuraIR format 1.10");
            if ((linkage & ~(VariadicMask | UserCast | OverloadBranch | Singleton | HasAttributes)) != 0 ||
                (linkage & VariadicMask) == VariadicMask) {
                fail("invalid source linkage flags");
            }
            value.variadic = static_cast<VariadicKind>(linkage & VariadicMask);
            value.user_cast = (linkage & UserCast) != 0;
            if ((linkage & OverloadBranch) != 0) value.overload_group = required_reference();
            if ((linkage & Singleton) != 0) value.singleton_instance = required_reference();
            if ((linkage & HasAttributes) != 0) {
                // An attribute is at least an id, a value kind, and one byte.
                const auto n = count(4);
                if (n == 0) fail("an attribute flag needs at least one attribute");
                value.attributes.reserve(n);
                for (std::uint32_t i = 0; i < n; ++i) {
                    const auto id = u16();
                    if (!find_attribute_info(id)) fail("unknown symbol attribute " + std::to_string(id));
                    value.attributes.push_back({static_cast<AttributeId>(id), attribute_value(0)});
                }
                if (const auto problem = attribute_problem(value.kind, value.attributes))
                    fail("symbol '" + value.name + "': " + *problem);
                if (hook_attribute(value) && minor_ < nir_hooks_minor)
                    fail("class hooks require NeuraIR format 1." + std::to_string(nir_hooks_minor));
                if (has_attribute(value, AttributeId::HostEffects) &&
                    (value.has_body || !value.external || !value.host_binding))
                    fail("symbol '" + value.name + "': a host-effects contract belongs to a host binding without a body");
                if (has_attribute(value, AttributeId::GeneratorResume) && minor_ < nir_lifetime_minor)
                    fail("symbol '" + value.name + "': generator.resume requires NeuraIR format 1." +
                         std::to_string(nir_lifetime_minor));
            }
        }
        return value;
    }

    IrAttributeValue attribute_value(std::size_t depth) {
        enter(depth);
        IrAttributeValue value;
        value.kind = static_cast<AttributeValueKind>(
            tag(static_cast<std::uint8_t>(AttributeValueKind::List), "attribute value"));
        switch (value.kind) {
        case AttributeValueKind::Bool:
            value.integer = u8();
            if (value.integer > 1) fail("invalid attribute bool " + std::to_string(value.integer));
            break;
        case AttributeValueKind::Signed:
        case AttributeValueKind::Unsigned: value.integer = u64(); break;
        case AttributeValueKind::Text: value.text = text(); break;
        case AttributeValueKind::Symbol: value.symbol = required_reference(); break;
        case AttributeValueKind::Type: value.type = type(depth + 1); break;
        case AttributeValueKind::Span: value.span = span(); break;
        case AttributeValueKind::List: {
            const auto n = count(2);
            value.items.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) value.items.push_back(attribute_value(depth + 1));
            break;
        }
        }
        return value;
    }

    std::vector<IrNode> nodes(std::size_t depth) {
        enter(depth);
        const auto n = count(1);
        std::vector<IrNode> values;
        values.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            values.push_back(node(depth));
        }
        return values;
    }

    IrNode node(std::size_t depth) {
        IrNode value{};
        if (minor_ >= 9) {
            const auto byte = u8();
            const auto operation = static_cast<std::uint8_t>(byte & ~poll_bit);
            if (operation > max_op_ || operation == static_cast<unsigned>(IrOp::HookRead) ||
                operation == static_cast<unsigned>(IrOp::HookAdmit))
                fail("invalid operation tag " + std::to_string(byte));
            value.op = static_cast<IrOp>(operation);
            value.poll = (byte & poll_bit) != 0;
            if (value.poll && !may_poll(value.op)) fail("a poll marks only a loop or a call");
        } else {
            value.op = static_cast<IrOp>(tag(max_op_, "operation"));
        }
        value.type = type(depth + 1);
        value.span = span();
        value.symbol = reference();
        value.text = text();
        if (linked_argument_annotation(value)) fail("argument transport is derived after linking");
        if (!aggregates_ && new_operator(value)) fail("bitwise operations require NeuraIR format 1.2");
        value.operands = nodes(depth + 1);
        if (minor_ < attribute_minor && is_deferred_initialize(value))
            fail("deferred initialization requires NeuraIR format 1.10");
        if (minor_ < 4 && class_qualified_load(value))
            fail("static fields with class reference qualifiers require NeuraIR format 1.4");
        value.body = nodes(depth + 1);
        value.alternative = nodes(depth + 1);
        if (minor_ < 4 && scoped_statements(value))
            fail("statements in a scoped value require NeuraIR format 1.4");
        if (minor_ < nir_any_minor && scoped_return(value))
            fail("a return inside a scoped value requires NeuraIR format 1." + std::to_string(nir_any_minor));
        if (value.op == IrOp::Loop && !value.alternative.empty() && !latches_) {
            fail("a loop latch requires NeuraIR format 1.1");
        }
        value.type_arguments = types(depth + 1);
        return value;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
    std::string origin_;
    std::vector<std::string> files_;
    std::uint32_t symbol_count_ = 0;
    std::uint16_t minor_ = 0;
    std::uint8_t max_op_ = max_op_1_0;
    bool latches_ = false;
    bool aggregates_ = false;
};

} // namespace

bool is_nir(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < magic.size()) {
        return false;
    }
    for (std::size_t i = 0; i < magic.size(); ++i) {
        if (bytes[i] != static_cast<std::uint8_t>(magic[i])) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> write_nir(const IrProgram& program) {
    return Writer(program).write(program);
}

IrProgram read_nir(std::span<const std::uint8_t> bytes, const std::string& origin,
                   std::uint16_t* minor_version, std::size_t proof_fuel) {
    auto program = Reader(bytes, origin).read(minor_version);
    if (!program.laws.empty()) {
        if (const auto issues = verify_ir(program); !issues.empty())
            throw NirError(origin + ": invalid NeuraIR for law checking: " + issues.front().message);
        check_program_laws(program, proof_fuel, LawCheckOrigin::Artifact);
    }
    return program;
}

} // namespace neura
