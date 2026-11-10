#include "nir_format.hpp"
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

std::uint16_t type_minor(const IrType& type) {
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
        if (op > max_op_1_3 || scoped_statements(node) || class_qualified_load(node)) return 4;
        if (op > max_op_1_2) {
            version = 3;
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
        if (version == 4) return version;
    }
    return version;
}

enum Flag : std::uint8_t {
    Exported = 1, Constant = 2, External = 4, HasBody = 8, HasPayload = 16, HostBound = 32, LibraryFile = 64,
    HasInterfacesOrBounds = 128
};

class Writer {
public:
    explicit Writer(const IrProgram& program) {
        // Span paths form a table in order of first use.
        for (const auto& symbol : program.symbols) {
            path(symbol.span.file);
        }
        for (const auto& body : program.bodies) {
            for (const auto& node : body.instructions) {
                paths(node);
            }
        }
    }

    std::vector<std::uint8_t> write(const IrProgram& program) {
        for (char c : magic) {
            u8(static_cast<std::uint8_t>(c));
        }
        u16(nir_major_version);
        std::uint16_t version = 0;
        for (const auto& item : program.symbols) {
            if (is_static_initializer(item)) {
                version = 5;
                break;
            }
            if (is_static_scope(item)) version = 4;
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
            if (version >= 4) break;
            version = std::max(version, required_minor(body.instructions));
        }
        u16(version);
        count(files_.size());
        for (const auto& file : files_) {
            text(file);
        }
        count(program.symbols.size());
        for (const auto& item : program.symbols) {
            symbol(item);
        }
        count(program.bodies.size());
        for (const auto& body : program.bodies) {
            reference(body.owner);
            nodes(body.instructions);
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

    void symbol(const IrSymbol& value) {
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
    }

    void nodes(const std::vector<IrNode>& values) {
        count(values.size());
        for (const auto& value : values) {
            node(value);
        }
    }

    void node(const IrNode& value) {
        u8(static_cast<std::uint8_t>(value.op));
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
};

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, std::string origin) : bytes_(bytes), origin_(std::move(origin)) {}

    IrProgram read() {
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
            max_op_1_0, max_op_1_1, max_op_1_2, max_op_1_3, max_op_1_4, max_op_1_4};
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
        if (position_ != bytes_.size()) {
            fail(std::to_string(bytes_.size() - position_) + " unread bytes at the end of the file");
        }
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
        if (type_minor(value) > minor_) fail("a structural function or generic enum type requires NeuraIR format 1.3");
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
        if ((flags & HostBound) != 0) value.host_binding = IrHostBinding{text(), u32()};
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
        value.op = static_cast<IrOp>(tag(max_op_, "operation"));
        value.type = type(depth + 1);
        value.span = span();
        value.symbol = reference();
        value.text = text();
        if (!aggregates_ && new_operator(value)) fail("bitwise operations require NeuraIR format 1.2");
        value.operands = nodes(depth + 1);
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

IrProgram read_nir(std::span<const std::uint8_t> bytes, const std::string& origin) {
    return Reader(bytes, origin).read();
}

} // namespace neura
