#include "nir_format.hpp"

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
constexpr std::uint8_t max_type_kind = static_cast<std::uint8_t>(TypeKind::Callable);
constexpr std::uint8_t max_symbol_kind = static_cast<std::uint8_t>(SymbolKind::Variant);
constexpr std::uint8_t max_op = static_cast<std::uint8_t>(IrOp::Discard);

enum Flag : std::uint8_t { Exported = 1, Constant = 2, External = 4, HasBody = 8 };

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
        u16(nir_minor_version);
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
        u8(static_cast<std::uint8_t>((value.exported ? Exported : 0) | (value.constant ? Constant : 0) |
                                     (value.external ? External : 0) | (value.has_body ? HasBody : 0)));
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
        const auto minor = u16();
        if (major != nir_major_version || minor != nir_minor_version) {
            fail("NeuraIR format " + std::to_string(major) + "." + std::to_string(minor) +
                 " is not supported; this reader accepts 1.0");
        }
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
        value.kind = static_cast<TypeKind>(tag(max_type_kind, "type kind"));
        value.name = text();
        value.symbol = reference();
        value.arguments = types(depth + 1);
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
        value.kind = static_cast<SymbolKind>(tag(max_symbol_kind, "symbol kind"));
        value.name = text();
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
        if ((flags & ~(Exported | Constant | External | HasBody)) != 0) {
            fail("invalid symbol flags");
        }
        value.exported = (flags & Exported) != 0;
        value.constant = (flags & Constant) != 0;
        value.external = (flags & External) != 0;
        value.has_body = (flags & HasBody) != 0;
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
        value.op = static_cast<IrOp>(tag(max_op, "operation"));
        value.type = type(depth + 1);
        value.span = span();
        value.symbol = reference();
        value.text = text();
        value.operands = nodes(depth + 1);
        value.body = nodes(depth + 1);
        value.alternative = nodes(depth + 1);
        value.type_arguments = types(depth + 1);
        return value;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
    std::string origin_;
    std::vector<std::string> files_;
    std::uint32_t symbol_count_ = 0;
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
