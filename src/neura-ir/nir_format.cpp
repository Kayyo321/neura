#include "nir_format.hpp"
#include "attributes.hpp"
#include "host_binding.hpp"
#include "integer.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <string_view>

namespace neura {
namespace {

constexpr std::string_view magic = "NEIR";
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
// NeuraIR 1.9 stores a node's poll in the top bit of its operation byte.
constexpr std::uint8_t poll_bit = 0x80;
constexpr std::uint16_t task_minor = 9;
// NeuraIR 1.10 adds symbol attributes and deferred initialization.
constexpr std::uint16_t attribute_minor = 10;
constexpr std::uint16_t newest_node_minor = attribute_minor;

bool may_poll(IrOp op) {
    return op == IrOp::Loop || op == IrOp::Call || op == IrOp::VirtualCall || op == IrOp::CallClosure;
}

std::uint16_t type_minor(const IrType& type) {
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

bool class_qualified_load(const IrNode& node) {
    return node.op == IrOp::Load && !node.operands.empty() &&
        node.operands.front().type.kind != TypeKind::Module;
}

std::uint16_t required_minor(const std::vector<IrNode>& nodes) {
    std::uint16_t version = 0;
    for (const auto& node : nodes) {
        const auto op = static_cast<std::uint8_t>(node.op);
        if (is_deferred_initialize(node)) return attribute_minor;
        if (op > max_op_1_7 || node.poll) {
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
    }

    std::vector<std::uint8_t> write(const IrProgram& program) {
        for (char c : magic) {
            u8(static_cast<std::uint8_t>(c));
        }
        u16(nir_major_version);
        std::uint16_t version = program.static_constants.empty() ? 0 : 6;
        if (program.native_library) version = 8;
        for (const auto& item : program.symbols) {
            if (item.variadic != VariadicKind::None || item.user_cast ||
                item.overload_group != no_symbol || item.singleton_instance != no_symbol) {
                version = std::max<std::uint16_t>(version, 6);
            }
            if (is_static_initializer(item)) version = std::max<std::uint16_t>(version, 5);
            if (is_static_scope(item)) version = std::max<std::uint16_t>(version, 4);
            if (host_callback(item)) version = std::max<std::uint16_t>(version, 8);
            if (!item.attributes.empty()) version = std::max(version, attribute_minor);
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
        constexpr std::uint8_t operation_limits[]{
            max_op_1_0, max_op_1_1, max_op_1_2, max_op_1_3, max_op_1_4, max_op_1_4, max_op_1_4, max_op_1_7,
            max_op_1_7, max_op_1_9, max_op_1_9};
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
        if (position_ != bytes_.size()) {
            fail(std::to_string(bytes_.size() - position_) + " unread bytes at the end of the file");
        }
        if (minor_version) *minor_version = minor_;
        return program;
    }

private:
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
        const auto max_kind = aggregates_ ? TypeKind::Tuple : TypeKind::Callable;
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
        const auto max_kind = minor_ >= 3 ? SymbolKind::Interface : SymbolKind::Variant;
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
            if (operation > max_op_) fail("invalid operation tag " + std::to_string(byte));
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
                   std::uint16_t* minor_version) {
    return Reader(bytes, origin).read(minor_version);
}

} // namespace neura
