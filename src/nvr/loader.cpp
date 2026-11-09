#include "format.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace neura::nvr {

std::string_view describe(LoadError::Kind kind) {
    switch (kind) {
    case LoadError::Kind::Malformed: return "malformed image";
    case LoadError::Kind::Unsupported: return "unsupported image";
    case LoadError::Kind::LimitExceeded: return "image exceeds a load limit";
    case LoadError::Kind::Invalid: return "invalid image";
    }
    return "invalid image";
}

bool valid_utf8(std::string_view bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto lead = static_cast<unsigned char>(bytes[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t code = 0;
        std::uint32_t minimum = 0;
        if ((lead & 0xe0) == 0xc0) {
            length = 2;
            code = lead & 0x1f;
            minimum = 0x80;
        } else if ((lead & 0xf0) == 0xe0) {
            length = 3;
            code = lead & 0x0f;
            minimum = 0x800;
        } else if ((lead & 0xf8) == 0xf0) {
            length = 4;
            code = lead & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (bytes.size() - i < length) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(bytes[i + k]);
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            return false;
        }
        i += length;
    }
    return true;
}

namespace {

using Kind = LoadError::Kind;

[[noreturn]] void fail(Kind kind, std::string message) {
    throw LoadError(kind, std::move(message));
}

std::string tag_name(const std::array<char, 4>& tag) {
    return std::string(tag.begin(), tag.end());
}

// Reads one section. Every count is checked against the bytes that remain,
// using the smallest encoding of one element, before storage is reserved.
class Reader {
public:
    Reader(std::span<const std::uint8_t> data, std::string section, const LoadLimits& limits)
        : data_(data), section_(std::move(section)), limits_(limits) {}

    std::uint8_t u8() { return static_cast<std::uint8_t>(little_endian(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(little_endian(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(little_endian(4)); }
    std::uint64_t u64() { return little_endian(8); }
    std::int64_t i64() { return static_cast<std::int64_t>(little_endian(8)); }

    std::uint32_t count(std::size_t minimum_element_size, std::string_view what) {
        const auto n = u32();
        if (n == absent) {
            fail(Kind::Malformed, section_ + ": " + std::string(what) + " count uses the reserved index");
        }
        if (n > limits_.max_vector_length) {
            fail(Kind::LimitExceeded, section_ + ": " + std::string(what) + " count " +
                                          std::to_string(n) + " exceeds the limit");
        }
        if (minimum_element_size != 0 && n > remaining() / minimum_element_size) {
            fail(Kind::Malformed, section_ + ": truncated " + std::string(what));
        }
        return n;
    }

    std::vector<std::uint8_t> bytes(std::string_view what) {
        const auto n = count(1, what);
        std::vector<std::uint8_t> result(data_.begin() + static_cast<std::ptrdiff_t>(position_),
                                         data_.begin() + static_cast<std::ptrdiff_t>(position_ + n));
        position_ += n;
        return result;
    }

    std::string text(std::string_view what) {
        const auto data = bytes(what);
        std::string result(data.begin(), data.end());
        if (!valid_utf8(result)) {
            fail(Kind::Malformed, section_ + ": " + std::string(what) + " is not valid UTF-8");
        }
        return result;
    }

    std::vector<std::uint32_t> indices(std::string_view what) {
        const auto n = count(4, what);
        std::vector<std::uint32_t> result;
        result.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            result.push_back(u32());
        }
        return result;
    }

    template <typename Read>
    auto vector(std::size_t minimum_element_size, std::string_view what, Read read) {
        const auto n = count(minimum_element_size, what);
        std::vector<decltype(read(*this))> result;
        result.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            result.push_back(read(*this));
        }
        return result;
    }

    void finish() const {
        if (position_ != data_.size()) {
            fail(Kind::Malformed, section_ + ": " + std::to_string(data_.size() - position_) +
                                      " unread bytes at the end of the section");
        }
    }

    const std::string& section() const { return section_; }
    const LoadLimits& limits() const { return limits_; }

private:
    std::size_t remaining() const { return data_.size() - position_; }

    std::uint64_t little_endian(std::size_t width) {
        if (remaining() < width) {
            fail(Kind::Malformed, section_ + ": truncated record");
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < width; ++i) {
            value |= std::uint64_t{data_[position_ + i]} << (8 * i);
        }
        position_ += width;
        return value;
    }

    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
    std::string section_;
    const LoadLimits& limits_;
};

// Minimum encoded sizes, used to bound counts before allocating.
constexpr std::size_t value_size = 9;
constexpr std::size_t assignment_size = 4 + value_size;
// A Return exit of a void function is only its kind and assignment count.
constexpr std::size_t exit_size = 1 + 4;
constexpr std::size_t block_size = 4 + 4 + 4 + 4;
constexpr std::size_t function_size = 4 + 4 + 4 + 4;
constexpr std::size_t node_size = 1 + 8 + 4;
constexpr std::size_t edge_size = 4 + 8;
constexpr std::size_t graph_size = 4 + 4 + 4;
constexpr std::size_t import_size = 4 + 4 + 4 + 4 + 4;
constexpr std::size_t location_size = 4 + 4 + 4 + 4 + 4;

Value read_value(Reader& in) {
    Value result;
    const auto kind = in.u8();
    if (kind > 2) {
        fail(Kind::Malformed, in.section() + ": invalid value descriptor kind " + std::to_string(kind));
    }
    result.kind = static_cast<ValueKind>(kind);
    result.type = in.u32();
    result.index = in.u32();
    return result;
}

std::vector<Value> read_values(Reader& in, std::string_view what) {
    return in.vector(value_size, what, read_value);
}

std::vector<Type> read_types(Reader& in) {
    return in.vector(1, "type table", [](Reader& r) {
        Type type;
        const auto kind = r.u8();
        if (kind > static_cast<std::uint8_t>(TypeKind::BFloat)) {
            fail(Kind::Malformed, "TYPE: invalid type kind " + std::to_string(kind));
        }
        type.kind = static_cast<TypeKind>(kind);
        switch (type.kind) {
        case TypeKind::Unsigned:
        case TypeKind::Signed:
            type.width = r.u8();
            if (type.width != 8 && type.width != 16 && type.width != 32 && type.width != 64) {
                fail(Kind::Malformed, "TYPE: invalid integer width " + std::to_string(type.width));
            }
            break;
        case TypeKind::Float:
            type.width = r.u8();
            if (type.width != 16 && type.width != 32 && type.width != 64) {
                fail(Kind::Malformed, "TYPE: invalid float width " + std::to_string(type.width));
            }
            break;
        case TypeKind::BFloat:
            type.width = r.u8();
            if (type.width != 16) {
                fail(Kind::Malformed, "TYPE: invalid bfloat width " + std::to_string(type.width));
            }
            break;
        case TypeKind::Object:
        case TypeKind::Array:
        case TypeKind::List:
            type.reference = r.u32();
            break;
        default:
            break;
        }
        return type;
    });
}

std::vector<Graph> read_graphs(Reader& in) {
    return in.vector(graph_size, "graph table", [](Reader& r) {
        Graph graph;
        graph.input_lanes = r.u32();
        graph.nodes = r.vector(node_size, "node list", [](Reader& n) {
            Node node;
            const auto activation = n.u8();
            if (activation > 1) {
                fail(Kind::Malformed, "GRPH: invalid activation " + std::to_string(activation));
            }
            node.activation = static_cast<Activation>(activation);
            node.bias = n.i64();
            node.edges = n.vector(edge_size, "edge list", [](Reader& e) {
                Edge edge;
                edge.source = e.u32();
                edge.weight = e.i64();
                return edge;
            });
            return node;
        });
        graph.outputs = r.indices("graph outputs");
        return graph;
    });
}

std::vector<Function> read_functions(Reader& in, const std::vector<Type>& types) {
    return in.vector(function_size, "function table", [&](Reader& r) {
        Function function;
        function.parameter_count = r.u32();
        function.result_type = r.u32();
        if (function.result_type >= types.size()) {
            fail(Kind::Invalid, "FUNC: function result type " + std::to_string(function.result_type) +
                                    " is out of range");
        }
        const bool returns_value = types[function.result_type].kind != TypeKind::Void;
        function.frame_types = r.indices("frame slot list");
        function.blocks = r.vector(block_size, "block list", [&](Reader& b) {
            Block block;
            block.graph = b.u32();
            block.inputs = read_values(b, "block input list");
            block.selector_start = b.u32();
            block.exits = b.vector(exit_size, "exit list", [&](Reader& e) {
                Exit exit;
                const auto kind = e.u8();
                if (kind > static_cast<std::uint8_t>(ExitKind::Trap)) {
                    fail(Kind::Malformed, "FUNC: invalid exit kind " + std::to_string(kind));
                }
                exit.kind = static_cast<ExitKind>(kind);
                exit.assignments = e.vector(assignment_size, "assignment list", [](Reader& a) {
                    Assignment assignment;
                    assignment.slot = a.u32();
                    assignment.value = read_value(a);
                    return assignment;
                });
                switch (exit.kind) {
                case ExitKind::Continue:
                    exit.resume = e.u32();
                    break;
                case ExitKind::Call:
                    exit.target = e.u32();
                    exit.arguments = read_values(e, "call argument list");
                    exit.result_slot = e.u32();
                    exit.resume = e.u32();
                    break;
                case ExitKind::Return:
                    if (returns_value) {
                        exit.arguments.push_back(read_value(e));
                    }
                    break;
                case ExitKind::State: {
                    const auto operation = e.u8();
                    if (operation >= state_op_count) {
                        fail(Kind::Malformed, "FUNC: invalid state operation " + std::to_string(operation));
                    }
                    exit.operation = static_cast<StateOp>(operation);
                    exit.immediate = e.u32();
                    exit.arguments = read_values(e, "state operation argument list");
                    exit.result_slot = e.u32();
                    exit.resume = e.u32();
                    break;
                }
                case ExitKind::Host:
                    exit.target = e.u32();
                    exit.arguments = read_values(e, "host call argument list");
                    exit.result_slot = e.u32();
                    exit.resume = e.u32();
                    exit.error_slot = e.u32();
                    exit.error_resume = e.u32();
                    break;
                case ExitKind::Trap:
                    exit.reason = e.u32();
                    break;
                }
                return exit;
            });
            return block;
        });
        return function;
    });
}

struct DirectoryEntry {
    std::array<char, 4> tag;
    std::uint32_t flags;
    std::uint64_t offset;
    std::uint64_t size;
};

struct KnownSection {
    const char* tag;
    bool required;
};

constexpr std::array<KnownSection, 9> known_sections{{
    {"CONS", true}, {"FUNC", true}, {"GLOB", true}, {"GRPH", true}, {"IMPT", true},
    {"LAYO", true}, {"META", true}, {"SMAP", false}, {"TYPE", true},
}};

const KnownSection* known(const std::array<char, 4>& tag) {
    for (const auto& section : known_sections) {
        if (std::equal(tag.begin(), tag.end(), section.tag)) {
            return &section;
        }
    }
    return nullptr;
}

// Checks the decoded records against each other: references, types,
// graph topology, layouts, continuations, and the entry signature.
class Validator {
public:
    explicit Validator(const Image& image) : image_(image) {}

    void run();

private:
    [[noreturn]] void invalid(const std::string& message) const { fail(Kind::Invalid, message); }

    const Type& type(std::uint32_t index, const std::string& where) const {
        if (index >= image_.types.size()) {
            invalid(where + ": type " + std::to_string(index) + " is out of range");
        }
        return image_.types[index];
    }

    void value_type(std::uint32_t index, const std::string& where) const {
        if (type(index, where).kind == TypeKind::Void) {
            invalid(where + ": void is not a value type");
        }
    }

    bool is_unsigned(std::uint32_t index, unsigned width) const {
        const auto& t = image_.types[index];
        return t.kind == TypeKind::Unsigned && t.width == width;
    }

    void types();
    void layouts();
    void constants();
    void globals();
    void graphs();
    void imports();
    void functions();
    void block(std::uint32_t function, std::uint32_t index);
    void value(const Function& function, const Block& block, const Value& item, const std::string& where) const;
    void assign(std::uint32_t actual, std::uint32_t expected, const std::string& where) const;
    void result(const Function& function, std::uint32_t slot, std::uint32_t produced, const std::string& where) const;
    void state(const Function& function, const Exit& exit, const std::string& where) const;
    void continuation(const Function& function, std::uint32_t target, const std::string& where) const;
    void entry();
    void source_map();

    const Image& image_;
    std::unique_ptr<TypeSystem> system_;
};

void Validator::run() {
    types();
    layouts();
    system_ = std::make_unique<TypeSystem>(image_);
    constants();
    globals();
    graphs();
    imports();
    functions();
    entry();
    source_map();
}

void Validator::types() {
    // References must name existing layouts and types, and element types
    // must not form a cycle.
    const auto count = image_.types.size();
    std::vector<std::uint8_t> state(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& t = image_.types[i];
        const auto where = "type " + std::to_string(i);
        if (t.kind == TypeKind::Object && t.reference >= image_.layouts.size()) {
            invalid(where + ": layout " + std::to_string(t.reference) + " is out of range");
        }
        if (t.kind == TypeKind::Array || t.kind == TypeKind::List) {
            value_type(t.reference, where + " element");
        }
    }
    for (std::size_t start = 0; start < count; ++start) {
        // Follow the element chain; each type has at most one successor.
        std::vector<std::size_t> path;
        auto current = start;
        while (state[current] == 0) {
            state[current] = 1;
            path.push_back(current);
            const auto& t = image_.types[current];
            if (t.kind != TypeKind::Array && t.kind != TypeKind::List) {
                break;
            }
            current = t.reference;
            if (state[current] == 1) {
                invalid("type " + std::to_string(start) + ": element types form a cycle");
            }
        }
        for (auto index : path) {
            state[index] = 2;
        }
    }
}

void Validator::layouts() {
    const auto count = image_.layouts.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& layout = image_.layouts[i];
        const auto where = "layout " + std::to_string(i);
        if (layout.parent != absent && layout.parent >= count) {
            invalid(where + ": parent " + std::to_string(layout.parent) + " is out of range");
        }
        for (auto field : layout.fields) {
            value_type(field, where + " field");
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto current = image_.layouts[i].parent;
        for (std::size_t steps = 0; current != absent; ++steps) {
            if (steps >= count) {
                invalid("layout " + std::to_string(i) + ": parent links form a cycle");
            }
            current = image_.layouts[current].parent;
        }
    }
    // The type system is needed for structural field comparison; build a
    // temporary one now that types and layouts are well formed.
    const TypeSystem system(image_);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& layout = image_.layouts[i];
        if (layout.parent == absent) {
            continue;
        }
        const auto& inherited = image_.layouts[layout.parent].fields;
        if (inherited.size() > layout.fields.size()) {
            invalid("layout " + std::to_string(i) + ": does not contain its parent's fields");
        }
        for (std::size_t f = 0; f < inherited.size(); ++f) {
            if (!system.same(inherited[f], layout.fields[f])) {
                invalid("layout " + std::to_string(i) + ": inherited field " + std::to_string(f) +
                        " changes type");
            }
        }
    }
}

void Validator::constants() {
    for (std::size_t i = 0; i < image_.constants.size(); ++i) {
        const auto& constant = image_.constants[i];
        const auto where = "constant " + std::to_string(i);
        const auto& t = type(constant.type, where);
        const auto size = constant.payload.size();
        switch (t.kind) {
        case TypeKind::Bool:
            if (size != 1 || constant.payload[0] > 1) {
                invalid(where + ": a Boolean payload is one byte, zero or one");
            }
            break;
        case TypeKind::Unsigned:
        case TypeKind::Signed:
            if (size != t.width / 8U) {
                invalid(where + ": an integer payload has " + std::to_string(t.width / 8U) + " bytes");
            }
            break;
        case TypeKind::Float:
        case TypeKind::BFloat:
            // Every bit pattern is a value, including NaN payloads.
            if (size != t.width / 8U) {
                invalid(where + ": a floating-point payload has " + std::to_string(t.width / 8U) + " bytes");
            }
            break;
        case TypeKind::Text:
            if (!valid_utf8(std::string(constant.payload.begin(), constant.payload.end()))) {
                invalid(where + ": text payload is not valid UTF-8");
            }
            break;
        case TypeKind::Bytes:
            break;
        default:
            invalid(where + ": constants cannot have this type");
        }
    }
}

void Validator::globals() {
    for (std::size_t i = 0; i < image_.globals.size(); ++i) {
        const auto& global = image_.globals[i];
        const auto where = "global " + std::to_string(i);
        value_type(global.type, where);
        if (global.initializer == absent) {
            continue;
        }
        if (global.initializer >= image_.constants.size()) {
            invalid(where + ": initializer " + std::to_string(global.initializer) + " is out of range");
        }
        if (!system_->same(image_.constants[global.initializer].type, global.type)) {
            invalid(where + ": initializer type does not match the global");
        }
    }
}

void Validator::graphs() {
    for (std::size_t g = 0; g < image_.graphs.size(); ++g) {
        const auto& graph = image_.graphs[g];
        const auto where = "graph " + std::to_string(g);
        const std::uint64_t wires = std::uint64_t{graph.input_lanes} + graph.nodes.size();
        if (wires >= absent) {
            invalid(where + ": too many wires");
        }
        for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
            const auto& node = graph.nodes[n];
            const std::uint64_t own = std::uint64_t{graph.input_lanes} + n;
            std::uint64_t previous = 0;
            for (std::size_t e = 0; e < node.edges.size(); ++e) {
                const auto source = node.edges[e].source;
                if (source >= own) {
                    invalid(where + " node " + std::to_string(n) + ": edge from wire " +
                            std::to_string(source) + " does not refer to an earlier wire");
                }
                if (e > 0 && source <= previous) {
                    invalid(where + " node " + std::to_string(n) +
                            ": edges are not sorted by unique source");
                }
                previous = source;
            }
        }
        for (auto output : graph.outputs) {
            if (output >= wires) {
                invalid(where + ": output wire " + std::to_string(output) + " is out of range");
            }
        }
    }
}

void Validator::imports() {
    std::set<std::pair<std::string, std::uint32_t>> seen;
    for (std::size_t i = 0; i < image_.imports.size(); ++i) {
        const auto& import = image_.imports[i];
        const auto where = "import " + std::to_string(i);
        if (!seen.emplace(import.name, import.interface_version).second) {
            invalid(where + ": duplicate import " + import.name + " version " +
                    std::to_string(import.interface_version));
        }
        for (auto parameter : import.parameter_types) {
            value_type(parameter, where + " parameter");
        }
        type(import.success_type, where + " success");
        type(import.error_type, where + " error");
    }
}

void Validator::assign(std::uint32_t actual, std::uint32_t expected, const std::string& where) const {
    if (!system_->assignable(actual, expected)) {
        invalid(where + ": type " + std::to_string(actual) + " is not assignable to type " +
                std::to_string(expected));
    }
}

void Validator::value(const Function& function, const Block& block, const Value& item,
                      const std::string& where) const {
    value_type(item.type, where);
    switch (item.kind) {
    case ValueKind::Slot:
        if (item.index >= function.frame_types.size()) {
            invalid(where + ": slot " + std::to_string(item.index) + " is out of range");
        }
        if (!system_->same(item.type, function.frame_types[item.index])) {
            invalid(where + ": descriptor type does not match slot " + std::to_string(item.index));
        }
        break;
    case ValueKind::Constant:
        if (item.index >= image_.constants.size()) {
            invalid(where + ": constant " + std::to_string(item.index) + " is out of range");
        }
        if (!system_->same(item.type, image_.constants[item.index].type)) {
            invalid(where + ": descriptor type does not match constant " + std::to_string(item.index));
        }
        break;
    case ValueKind::Lanes: {
        const auto& graph = image_.graphs[block.graph];
        const std::uint64_t end = std::uint64_t{item.index} + lane_count(image_.types[item.type]);
        if (end > graph.outputs.size()) {
            invalid(where + ": lanes " + std::to_string(item.index) + ".." + std::to_string(end) +
                    " exceed the graph's " + std::to_string(graph.outputs.size()) + " outputs");
        }
        break;
    }
    }
}

void Validator::result(const Function& function, std::uint32_t slot, std::uint32_t produced,
                       const std::string& where) const {
    if (image_.types[produced].kind == TypeKind::Void) {
        if (slot != absent) {
            invalid(where + ": a void result requires an absent slot");
        }
        return;
    }
    if (slot == absent || slot >= function.frame_types.size()) {
        invalid(where + ": a nonvoid result requires a slot in the frame");
    }
    assign(produced, function.frame_types[slot], where + " result");
}

void Validator::continuation(const Function& function, std::uint32_t target, const std::string& where) const {
    if (target >= function.blocks.size()) {
        invalid(where + ": block " + std::to_string(target) + " is out of range");
    }
}

void Validator::state(const Function& function, const Exit& exit,
                      const std::string& where) const {
    const auto& args = exit.arguments;
    auto arity = [&](std::size_t expected) {
        if (args.size() != expected) {
            invalid(where + ": expects " + std::to_string(expected) + " argument(s)");
        }
    };
    auto zero_immediate = [&] {
        if (exit.immediate != 0) {
            invalid(where + ": immediate must be zero");
        }
    };
    auto kind_of = [&](std::size_t argument) { return image_.types[args[argument].type].kind; };
    auto require_kind = [&](std::size_t argument, std::initializer_list<TypeKind> kinds) {
        if (std::find(kinds.begin(), kinds.end(), kind_of(argument)) == kinds.end()) {
            invalid(where + ": argument " + std::to_string(argument) + " has the wrong type");
        }
    };
    auto require_index = [&](std::size_t argument) {
        if (!is_unsigned(args[argument].type, 64)) {
            invalid(where + ": argument " + std::to_string(argument) + " must be an unsigned 64-bit index");
        }
    };
    auto no_result = [&] {
        if (exit.result_slot != absent) {
            invalid(where + ": a void result requires an absent slot");
        }
    };
    auto result_of = [&](std::uint32_t produced) { result(function, exit.result_slot, produced, where); };
    auto require_slot_type = [&](auto predicate, const char* what) {
        if (exit.result_slot == absent || exit.result_slot >= function.frame_types.size() ||
            !predicate(function.frame_types[exit.result_slot])) {
            invalid(where + ": result slot must have type " + what);
        }
    };
    auto field_types = [&](std::size_t argument) -> const std::vector<std::uint32_t>& {
        return image_.layouts[image_.types[args[argument].type].reference].fields;
    };
    auto element_type = [&](std::size_t argument) { return image_.types[args[argument].type].reference; };

    switch (exit.operation) {
    case StateOp::AllocateObject:
        arity(0);
        if (exit.immediate >= image_.layouts.size()) {
            invalid(where + ": layout " + std::to_string(exit.immediate) + " is out of range");
        }
        require_slot_type([&](std::uint32_t slot) {
            const auto& t = image_.types[slot];
            return t.kind == TypeKind::Object && system_->derives(exit.immediate, t.reference);
        }, "a reference to the allocated layout");
        break;
    case StateOp::ReadTag:
        arity(1);
        zero_immediate();
        require_kind(0, {TypeKind::Object});
        require_slot_type([&](std::uint32_t slot) { return is_unsigned(slot, 32); }, "uint32");
        break;
    case StateOp::ReadField:
        arity(1);
        require_kind(0, {TypeKind::Object});
        if (exit.immediate >= field_types(0).size()) {
            invalid(where + ": field " + std::to_string(exit.immediate) + " is out of range");
        }
        result_of(field_types(0)[exit.immediate]);
        break;
    case StateOp::WriteField:
        arity(2);
        require_kind(0, {TypeKind::Object});
        if (exit.immediate >= field_types(0).size()) {
            invalid(where + ": field " + std::to_string(exit.immediate) + " is out of range");
        }
        assign(args[1].type, field_types(0)[exit.immediate], where + " field value");
        no_result();
        break;
    case StateOp::FreeObject:
        arity(1);
        zero_immediate();
        require_kind(0, {TypeKind::Object});
        no_result();
        break;
    case StateOp::ReadGlobal:
        arity(0);
        if (exit.immediate >= image_.globals.size()) {
            invalid(where + ": global " + std::to_string(exit.immediate) + " is out of range");
        }
        result_of(image_.globals[exit.immediate].type);
        break;
    case StateOp::WriteGlobal:
        arity(1);
        if (exit.immediate >= image_.globals.size()) {
            invalid(where + ": global " + std::to_string(exit.immediate) + " is out of range");
        }
        assign(args[0].type, image_.globals[exit.immediate].type, where + " global value");
        no_result();
        break;
    case StateOp::AllocateCollection: {
        if (exit.immediate >= image_.types.size()) {
            invalid(where + ": type " + std::to_string(exit.immediate) + " is out of range");
        }
        const auto& collection = image_.types[exit.immediate];
        if (collection.kind != TypeKind::Array && collection.kind != TypeKind::List) {
            invalid(where + ": immediate must name an array or list type");
        }
        for (std::size_t i = 0; i < args.size(); ++i) {
            assign(args[i].type, collection.reference, where + " element " + std::to_string(i));
        }
        result_of(exit.immediate);
        break;
    }
    case StateOp::ReadLength:
        arity(1);
        zero_immediate();
        require_kind(0, {TypeKind::Array, TypeKind::List, TypeKind::Text, TypeKind::Bytes});
        require_slot_type([&](std::uint32_t slot) { return is_unsigned(slot, 64); }, "uint64");
        break;
    case StateOp::ReadElement:
        arity(2);
        zero_immediate();
        require_kind(0, {TypeKind::Array, TypeKind::List, TypeKind::Text, TypeKind::Bytes});
        require_index(1);
        if (kind_of(0) == TypeKind::Text || kind_of(0) == TypeKind::Bytes) {
            require_slot_type([&](std::uint32_t slot) { return is_unsigned(slot, 8); }, "uint8");
        } else {
            result_of(element_type(0));
        }
        break;
    case StateOp::WriteElement:
        arity(3);
        zero_immediate();
        require_kind(0, {TypeKind::Array, TypeKind::List});
        require_index(1);
        assign(args[2].type, element_type(0), where + " element value");
        no_result();
        break;
    case StateOp::AppendElement:
        arity(2);
        zero_immediate();
        require_kind(0, {TypeKind::List});
        assign(args[1].type, element_type(0), where + " element value");
        no_result();
        break;
    case StateOp::RemoveElement:
        arity(2);
        zero_immediate();
        require_kind(0, {TypeKind::List});
        require_index(1);
        no_result();
        break;
    case StateOp::ReadVersion:
        arity(1);
        zero_immediate();
        require_kind(0, {TypeKind::List});
        require_slot_type([&](std::uint32_t slot) { return is_unsigned(slot, 64); }, "uint64");
        break;
    case StateOp::ConstructText: {
        arity(1);
        if (exit.immediate >= image_.types.size()) {
            invalid(where + ": type " + std::to_string(exit.immediate) + " is out of range");
        }
        const auto kind = image_.types[exit.immediate].kind;
        if (kind != TypeKind::Text && kind != TypeKind::Bytes) {
            invalid(where + ": immediate must name a text or bytes type");
        }
        require_kind(0, {TypeKind::Array, TypeKind::List});
        if (!is_unsigned(element_type(0), 8)) {
            invalid(where + ": argument must hold uint8 elements");
        }
        result_of(exit.immediate);
        break;
    }
    }
}

void Validator::block(std::uint32_t function_index, std::uint32_t index) {
    const auto& function = image_.functions[function_index];
    const auto& item = function.blocks[index];
    const auto where = "function " + std::to_string(function_index) + " block " + std::to_string(index);
    if (item.graph >= image_.graphs.size()) {
        invalid(where + ": graph " + std::to_string(item.graph) + " is out of range");
    }
    const auto& graph = image_.graphs[item.graph];
    std::uint64_t lanes = 0;
    for (std::size_t i = 0; i < item.inputs.size(); ++i) {
        const auto& input = item.inputs[i];
        const auto input_where = where + " input " + std::to_string(i);
        if (input.kind == ValueKind::Lanes) {
            invalid(input_where + ": only frame slots and constants may supply graph inputs");
        }
        value(function, item, input, input_where);
        lanes += lane_count(image_.types[input.type]);
    }
    if (lanes != graph.input_lanes) {
        invalid(where + ": inputs supply " + std::to_string(lanes) + " lanes but the graph takes " +
                std::to_string(graph.input_lanes));
    }
    if (item.exits.empty()) {
        invalid(where + ": a block needs at least one exit");
    }
    if (item.exits.size() == 1) {
        if (item.selector_start != 0) {
            invalid(where + ": a block with one exit has selector start zero");
        }
    } else if (std::uint64_t{item.selector_start} + selector_lanes(item.exits.size()) > graph.outputs.size()) {
        invalid(where + ": selector lanes exceed the graph outputs");
    }

    for (std::size_t e = 0; e < item.exits.size(); ++e) {
        const auto& exit = item.exits[e];
        const auto exit_where = where + " exit " + std::to_string(e);
        std::set<std::uint32_t> destinations;
        if (exit.kind == ExitKind::Trap && !exit.assignments.empty()) {
            invalid(exit_where + ": a trap exit has no assignments");
        }
        for (const auto& assignment : exit.assignments) {
            if (assignment.slot >= function.frame_types.size()) {
                invalid(exit_where + ": destination slot " + std::to_string(assignment.slot) + " is out of range");
            }
            if (!destinations.insert(assignment.slot).second) {
                invalid(exit_where + ": slot " + std::to_string(assignment.slot) + " is assigned twice");
            }
            value(function, item, assignment.value, exit_where + " assignment");
            assign(assignment.value.type, function.frame_types[assignment.slot], exit_where + " assignment");
        }
        for (std::size_t a = 0; a < exit.arguments.size(); ++a) {
            value(function, item, exit.arguments[a], exit_where + " argument " + std::to_string(a));
        }
        switch (exit.kind) {
        case ExitKind::Continue:
            continuation(function, exit.resume, exit_where);
            break;
        case ExitKind::Call: {
            if (exit.target >= image_.functions.size()) {
                invalid(exit_where + ": function " + std::to_string(exit.target) + " is out of range");
            }
            const auto& callee = image_.functions[exit.target];
            if (exit.arguments.size() != callee.parameter_count) {
                invalid(exit_where + ": expects " + std::to_string(callee.parameter_count) + " argument(s)");
            }
            for (std::size_t a = 0; a < exit.arguments.size(); ++a) {
                assign(exit.arguments[a].type, callee.frame_types[a], exit_where + " argument " + std::to_string(a));
            }
            result(function, exit.result_slot, callee.result_type, exit_where);
            continuation(function, exit.resume, exit_where);
            break;
        }
        case ExitKind::Return:
            if (!exit.arguments.empty()) {
                assign(exit.arguments.front().type, function.result_type, exit_where + " value");
            }
            break;
        case ExitKind::State:
            state(function, exit, exit_where);
            continuation(function, exit.resume, exit_where);
            break;
        case ExitKind::Host: {
            if (exit.target >= image_.imports.size()) {
                invalid(exit_where + ": import " + std::to_string(exit.target) + " is out of range");
            }
            const auto& import = image_.imports[exit.target];
            if (exit.arguments.size() != import.parameter_types.size()) {
                invalid(exit_where + ": expects " + std::to_string(import.parameter_types.size()) +
                        " argument(s)");
            }
            for (std::size_t a = 0; a < exit.arguments.size(); ++a) {
                assign(exit.arguments[a].type, import.parameter_types[a],
                       exit_where + " argument " + std::to_string(a));
            }
            result(function, exit.result_slot, import.success_type, exit_where + " success");
            result(function, exit.error_slot, import.error_type, exit_where + " error");
            continuation(function, exit.resume, exit_where + " success");
            continuation(function, exit.error_resume, exit_where + " error");
            break;
        }
        case ExitKind::Trap:
            if (exit.reason < 1 || exit.reason > 5) {
                invalid(exit_where + ": trap reason " + std::to_string(exit.reason) + " is not defined");
            }
            break;
        }
    }
}

void Validator::functions() {
    for (std::size_t f = 0; f < image_.functions.size(); ++f) {
        const auto& function = image_.functions[f];
        const auto where = "function " + std::to_string(f);
        if (function.parameter_count > function.frame_types.size()) {
            invalid(where + ": more parameters than frame slots");
        }
        for (auto slot : function.frame_types) {
            value_type(slot, where + " frame slot");
        }
        if (function.blocks.empty()) {
            invalid(where + ": a function needs an entry block");
        }
    }
    for (std::uint32_t f = 0; f < image_.functions.size(); ++f) {
        for (std::uint32_t b = 0; b < image_.functions[f].blocks.size(); ++b) {
            block(f, b);
        }
    }
}

void Validator::entry() {
    if (image_.entry >= image_.functions.size()) {
        invalid("entry function " + std::to_string(image_.entry) + " is out of range");
    }
    const auto& function = image_.functions[image_.entry];
    const auto& result_type = image_.types[function.result_type];
    bool takes_arguments = function.parameter_count == 1;
    if (takes_arguments) {
        const auto& argument = image_.types[function.frame_types[0]];
        takes_arguments = argument.kind == TypeKind::Array &&
                          image_.types[argument.reference].kind == TypeKind::Text;
    }
    if (!takes_arguments || result_type.kind != TypeKind::Signed || result_type.width != 32) {
        invalid("the entry function must take one UTF-8 text array and return int32");
    }
}

void Validator::source_map() {
    if (!image_.source_map) {
        return;
    }
    const auto& entries = *image_.source_map;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const auto where = "source map entry " + std::to_string(i);
        if (entry.function >= image_.functions.size() ||
            entry.block >= image_.functions[entry.function].blocks.size()) {
            invalid(where + ": block reference is out of range");
        }
        if (entry.line == 0 || entry.column == 0) {
            invalid(where + ": lines and columns start at one");
        }
        if (i > 0 && std::tie(entries[i - 1].function, entries[i - 1].block) >= std::tie(entry.function, entry.block)) {
            invalid(where + ": entries are not sorted and unique by function and block");
        }
    }
}

} // namespace

TypeSystem::TypeSystem(const Image& image) : image_(&image), canonical_(image.types.size(), absent) {
    std::map<std::tuple<TypeKind, std::uint8_t, std::uint32_t>, std::uint32_t> representatives;
    const auto collection = [](TypeKind kind) { return kind == TypeKind::Array || kind == TypeKind::List; };
    // Element types are acyclic. Resolve each chain from its innermost
    // element outward, without recursion, so a long chain cannot exhaust
    // the stack.
    std::vector<std::uint32_t> chain;
    for (std::uint32_t start = 0; start < image.types.size(); ++start) {
        chain.clear();
        for (auto index = start; canonical_[index] == absent;) {
            chain.push_back(index);
            if (!collection(image.types[index].kind)) {
                break;
            }
            index = image.types[index].reference;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const auto& t = image.types[*it];
            const auto reference = collection(t.kind) ? canonical_[t.reference] : t.reference;
            const auto [representative, inserted] = representatives.emplace(std::make_tuple(t.kind, t.width, reference), *it);
            canonical_[*it] = representative->second;
        }
    }
}

bool TypeSystem::derives(std::uint32_t derived, std::uint32_t base) const {
    for (auto current = derived; current != absent; current = image_->layouts[current].parent) {
        if (current == base) {
            return true;
        }
    }
    return false;
}

bool TypeSystem::assignable(std::uint32_t actual, std::uint32_t expected) const {
    if (same(actual, expected)) {
        return true;
    }
    const auto& from = image_->types[actual];
    const auto& to = image_->types[expected];
    return from.kind == TypeKind::Object && to.kind == TypeKind::Object && derives(from.reference, to.reference);
}

Image load(std::span<const std::uint8_t> bytes, const LoadLimits& limits) {
    constexpr std::size_t header_size = 24;
    constexpr std::size_t directory_record_size = 24;
    if (bytes.size() > limits.max_image_bytes) {
        fail(Kind::LimitExceeded, "image of " + std::to_string(bytes.size()) + " bytes exceeds the limit of " +
                                      std::to_string(limits.max_image_bytes));
    }
    Reader header(bytes, "header", limits);
    if (bytes.size() < header_size) {
        fail(Kind::Malformed, "header: truncated header");
    }
    std::array<char, 4> magic{};
    for (auto& c : magic) {
        c = static_cast<char>(header.u8());
    }
    if (tag_name(magic) != "NEUR") {
        fail(Kind::Malformed, "header: not an NVR image");
    }
    const auto major = header.u16();
    const auto minor = header.u16();
    if (major != major_version || minor > minor_version) {
        fail(Kind::Unsupported, "header: format version " + std::to_string(major) + "." + std::to_string(minor) +
                                    " is not supported; this loader accepts 1.0 through 1." +
                                    std::to_string(minor_version));
    }
    const auto profile = header.u32();
    if (profile != graph_profile) {
        fail(Kind::Unsupported, "header: graph profile " + std::to_string(profile) + " is not supported");
    }
    const auto section_count = header.u32();
    const auto file_size = header.u64();
    if (file_size != bytes.size()) {
        fail(Kind::Malformed, "header: file size field is " + std::to_string(file_size) + " but the image has " +
                                  std::to_string(bytes.size()) + " bytes");
    }
    if (section_count > (bytes.size() - header_size) / directory_record_size) {
        fail(Kind::Malformed, "header: truncated section directory");
    }

    std::vector<DirectoryEntry> directory;
    directory.reserve(section_count);
    for (std::uint32_t i = 0; i < section_count; ++i) {
        DirectoryEntry entry{};
        for (auto& c : entry.tag) {
            c = static_cast<char>(header.u8());
            if (c < 0x20 || c > 0x7e) {
                fail(Kind::Malformed, "directory: section tag is not printable ASCII");
            }
        }
        entry.flags = header.u32();
        entry.offset = header.u64();
        entry.size = header.u64();
        if ((entry.flags & ~1U) != 0) {
            fail(Kind::Malformed, "directory: section " + tag_name(entry.tag) + " sets reserved flag bits");
        }
        for (const auto& previous : directory) {
            if (previous.tag == entry.tag) {
                fail(Kind::Malformed, "directory: duplicate section " + tag_name(entry.tag));
            }
        }
        if (!directory.empty() && !(directory.back().tag < entry.tag)) {
            fail(Kind::Malformed, "directory: section " + tag_name(entry.tag) + " is out of ascending tag order");
        }
        directory.push_back(entry);
    }

    std::uint64_t expected = header_size + std::uint64_t{directory_record_size} * section_count;
    std::map<std::string, std::span<const std::uint8_t>> sections;
    std::vector<RawSection> other_sections;
    for (const auto& entry : directory) {
        const auto name = tag_name(entry.tag);
        if (entry.offset != expected) {
            fail(Kind::Malformed, "directory: section " + name + " starts at " + std::to_string(entry.offset) +
                                      " instead of " + std::to_string(expected) + " (gap or overlap)");
        }
        if (entry.size > bytes.size() - entry.offset) {
            fail(Kind::Malformed, "directory: section " + name + " extends past the end of the image");
        }
        expected += entry.size;
        const auto* section = known(entry.tag);
        const bool required = (entry.flags & 1U) != 0;
        if (section == nullptr) {
            if (required) {
                fail(Kind::Unsupported, "directory: unknown required section " + name);
            }
            const auto data = bytes.subspan(static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.size));
            other_sections.push_back({entry.tag, {data.begin(), data.end()}});
            continue;
        }
        if (section->required != required) {
            fail(Kind::Malformed, "directory: section " + name + " has the wrong required flag");
        }
        sections.emplace(name, bytes.subspan(static_cast<std::size_t>(entry.offset),
                                             static_cast<std::size_t>(entry.size)));
    }
    if (expected != bytes.size()) {
        fail(Kind::Malformed, "directory: " + std::to_string(bytes.size() - expected) +
                                  " bytes follow the last section");
    }
    for (const auto& section : known_sections) {
        if (section.required && !sections.contains(section.tag)) {
            fail(Kind::Malformed, std::string("directory: missing required section ") + section.tag);
        }
    }

    Image image;
    image.minor_version = minor;
    image.other_sections = std::move(other_sections);
    auto section = [&](const char* tag, auto read) {
        Reader in(sections.at(tag), tag, limits);
        read(in);
        in.finish();
    };
    section("TYPE", [&](Reader& in) { image.types = read_types(in); });
    section("LAYO", [&](Reader& in) {
        image.layouts = in.vector(8, "layout table", [](Reader& r) {
            Layout layout;
            layout.parent = r.u32();
            layout.fields = r.indices("field list");
            return layout;
        });
    });
    section("CONS", [&](Reader& in) {
        image.constants = in.vector(8, "constant table", [](Reader& r) {
            Constant constant;
            constant.type = r.u32();
            constant.payload = r.bytes("constant payload");
            return constant;
        });
    });
    section("GLOB", [&](Reader& in) {
        image.globals = in.vector(8, "global table", [](Reader& r) {
            Global global;
            global.type = r.u32();
            global.initializer = r.u32();
            return global;
        });
    });
    section("GRPH", [&](Reader& in) {
        image.graphs = read_graphs(in);
        for (const auto& graph : image.graphs) {
            if (std::uint64_t{graph.input_lanes} + graph.nodes.size() > limits.max_graph_wires) {
                fail(Kind::LimitExceeded, "GRPH: a graph exceeds the wire limit");
            }
        }
    });
    section("IMPT", [&](Reader& in) {
        image.imports = in.vector(import_size, "import table", [](Reader& r) {
            Import import;
            import.name = r.text("import name");
            import.interface_version = r.u32();
            import.parameter_types = r.indices("import parameter list");
            import.success_type = r.u32();
            import.error_type = r.u32();
            return import;
        });
    });
    section("FUNC", [&](Reader& in) { image.functions = read_functions(in, image.types); });
    section("META", [&](Reader& in) { image.entry = in.u32(); });
    if (sections.contains("SMAP")) {
        section("SMAP", [&](Reader& in) {
            image.source_map = in.vector(location_size, "source map", [](Reader& r) {
                SourceLocation location;
                location.function = r.u32();
                location.block = r.u32();
                location.path = r.text("source path");
                location.line = r.u32();
                location.column = r.u32();
                return location;
            });
        });
    }

    if (required_minor_version(image) > minor) {
        fail(Kind::Malformed, "header: the image uses NVR 1." + std::to_string(required_minor_version(image)) +
                                  " features but declares 1." + std::to_string(minor));
    }
    Validator(image).run();
    return image;
}

} // namespace neura::nvr
