#include "assembly.hpp"

#include "nvr/format.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <optional>
#include <sstream>
#include <vector>

namespace neura::decompiler {
namespace {

using nvr::absent;

constexpr std::array<std::string_view, 11> type_kinds{"void", "bool", "unsigned", "signed", "text", "bytes",
                                                       "object", "array", "list", "float", "bfloat"};
constexpr std::array<std::string_view, 7> exit_kinds{
    "continue", "call", "return", "state", "host", "trap", "message-trap",
};
constexpr std::array<std::string_view, nvr::state_op_count> state_ops{
    "allocate-object", "read-tag",         "read-field",    "write-field",    "free-object",
    "read-global",     "write-global",     "allocate-collection", "read-length", "read-element",
    "write-element",   "append-element",   "remove-element", "construct-text", "read-version",
    "text-slice",      "text-from-utf8",   "text-equal",    "class-hash",     "class-text",
    "class-is",        "class-cast",       "scalar-text", "repeat-collection",
};
constexpr std::array<std::string_view, 3> value_kinds{"slot", "constant", "lanes"};

std::string index_or_absent(std::uint32_t value) {
    return value == absent ? "-" : std::to_string(value);
}

std::string quoted(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (byte < 0x20 || byte == 0x7f) {
            constexpr char digits[] = "0123456789abcdef";
            result += "\\x";
            result += digits[byte >> 4];
            result += digits[byte & 15];
        } else {
            result += c;
        }
    }
    return result + "\"";
}

std::string hex(const std::vector<std::uint8_t>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result.empty() ? "-" : result;
}

std::string value(const nvr::Value& item) {
    return std::string(value_kinds[static_cast<std::size_t>(item.kind)]) + ":" + std::to_string(item.type) + ":" +
           std::to_string(item.index);
}

template <typename T, typename Format>
std::string list(const std::vector<T>& items, Format format) {
    std::string result = "[";
    for (const auto& item : items) {
        result += " " + format(item);
    }
    return result + " ]";
}

std::string indices(const std::vector<std::uint32_t>& items) {
    return list(items, [](std::uint32_t item) { return std::to_string(item); });
}

std::string values(const std::vector<nvr::Value>& items) {
    return list(items, value);
}

// A readable rendering of a constant, as a comment after its bytes.
std::string constant_comment(const nvr::Image& image, const nvr::Constant& constant) {
    if (constant.type >= image.types.size()) {
        return {};
    }
    const auto& type = image.types[constant.type];
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < constant.payload.size() && i < 8; ++i) {
        bits |= std::uint64_t{constant.payload[i]} << (8 * i);
    }
    switch (type.kind) {
    case nvr::TypeKind::Bool: return bits != 0 ? "true" : "false";
    case nvr::TypeKind::Unsigned: return std::to_string(bits);
    case nvr::TypeKind::Signed: {
        const auto width = type.width;
        const auto value = width < 64 && ((bits >> (width - 1)) & 1U) != 0 ? static_cast<std::int64_t>(bits | ~((std::uint64_t{1} << width) - 1))
                                                                           : static_cast<std::int64_t>(bits);
        return std::to_string(value);
    }
    case nvr::TypeKind::Text: return quoted(std::string(constant.payload.begin(), constant.payload.end()));
    case nvr::TypeKind::Object: return constant.payload.empty() ? "nil" : std::string{};
    default: return {};
    }
}

class Tokens {
public:
    Tokens(std::string_view line, std::size_t number) : number_(number) {
        std::size_t i = 0;
        while (i < line.size()) {
            const char c = line[i];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i;
            } else if (c == ';') {
                break;
            } else if (c == '[' || c == ']') {
                tokens_.emplace_back(1, c);
                ++i;
            } else if (c == '"') {
                std::string text;
                ++i;
                for (;;) {
                    if (i >= line.size()) {
                        throw AssemblyError(number_, "unterminated string");
                    }
                    if (line[i] == '"') {
                        ++i;
                        break;
                    }
                    if (line[i] == '\\') {
                        if (i + 1 >= line.size()) {
                            throw AssemblyError(number_, "unterminated escape");
                        }
                        if (line[i + 1] == 'x') {
                            if (i + 3 >= line.size()) {
                                throw AssemblyError(number_, "truncated \\x escape");
                            }
                            text.push_back(static_cast<char>(byte_of(line[i + 2], line[i + 3])));
                            i += 4;
                        } else {
                            text.push_back(line[i + 1]);
                            i += 2;
                        }
                        continue;
                    }
                    text.push_back(line[i++]);
                }
                strings_.push_back(tokens_.size());
                tokens_.push_back(std::move(text));
            } else {
                const auto start = i;
                while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '[' &&
                       line[i] != ']' && line[i] != ';') {
                    ++i;
                }
                tokens_.emplace_back(line.substr(start, i - start));
            }
        }
    }

    bool empty() const { return tokens_.empty(); }
    bool done() const { return position_ == tokens_.size(); }
    std::size_t line() const { return number_; }

    [[noreturn]] void error(const std::string& message) const { throw AssemblyError(number_, message); }

    std::string next(std::string_view what) {
        if (done()) {
            error("expected " + std::string(what));
        }
        return tokens_[position_++];
    }

    std::string string(std::string_view what) {
        if (done() || std::find(strings_.begin(), strings_.end(), position_) == strings_.end()) {
            error("expected a quoted " + std::string(what));
        }
        return tokens_[position_++];
    }

    void keyword(std::string_view word) {
        const auto token = next(word);
        if (token != word) {
            error("expected '" + std::string(word) + "', found '" + token + "'");
        }
    }

    bool peek(std::string_view word) const { return !done() && tokens_[position_] == word; }

    template <typename T>
    T number(std::string_view what) {
        const auto token = next(what);
        T result{};
        const auto [end, failure] = std::from_chars(token.data(), token.data() + token.size(), result);
        if (failure != std::errc{} || end != token.data() + token.size()) {
            error("expected " + std::string(what) + ", found '" + token + "'");
        }
        return result;
    }

    std::uint32_t index(std::string_view what) { return number<std::uint32_t>(what); }

    std::uint32_t index_or_absent(std::string_view what) {
        if (peek("-")) {
            ++position_;
            return absent;
        }
        return index(what);
    }

    void expect_index(std::uint32_t expected, std::string_view table) {
        const auto actual = index(std::string(table) + " index");
        if (actual != expected) {
            error(std::string(table) + " " + std::to_string(actual) + " appears where " + std::string(table) + " " +
                  std::to_string(expected) + " belongs");
        }
    }

    template <typename Read>
    auto list(Read read) {
        keyword("[");
        std::vector<decltype(read())> result;
        while (!peek("]")) {
            if (done()) {
                error("unterminated list");
            }
            result.push_back(read());
        }
        ++position_;
        return result;
    }

    std::vector<std::uint32_t> indices(std::string_view what) {
        return list([&] { return index(what); });
    }

    template <std::size_t N>
    std::size_t choice(const std::array<std::string_view, N>& names, std::string_view what) {
        const auto token = next(what);
        for (std::size_t i = 0; i < N; ++i) {
            if (names[i] == token) {
                return i;
            }
        }
        error("unknown " + std::string(what) + " '" + token + "'");
    }

    nvr::Value value() {
        const auto token = next("value");
        const auto first = token.find(':');
        const auto second = token.find(':', first == std::string::npos ? 0 : first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            error("expected a value of the form kind:type:index, found '" + token + "'");
        }
        nvr::Value result;
        const auto kind = token.substr(0, first);
        std::size_t k = 0;
        while (k < value_kinds.size() && value_kinds[k] != kind) {
            ++k;
        }
        if (k == value_kinds.size()) {
            error("unknown value kind '" + kind + "'");
        }
        result.kind = static_cast<nvr::ValueKind>(k);
        const auto parse = [&](std::string_view digits) {
            std::uint32_t number = 0;
            const auto [end, failure] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
            if (failure != std::errc{} || end != digits.data() + digits.size()) {
                error("malformed value '" + token + "'");
            }
            return number;
        };
        result.type = parse(std::string_view(token).substr(first + 1, second - first - 1));
        result.index = parse(std::string_view(token).substr(second + 1));
        return result;
    }

    std::vector<std::uint8_t> bytes() {
        const auto token = next("hex bytes");
        if (token == "-") {
            return {};
        }
        if (token.size() % 2 != 0) {
            error("hex bytes have an odd number of digits");
        }
        std::vector<std::uint8_t> result;
        for (std::size_t i = 0; i < token.size(); i += 2) {
            result.push_back(byte_of(token[i], token[i + 1]));
        }
        return result;
    }

    void finish() const {
        if (!done()) {
            error("unexpected '" + tokens_[position_] + "'");
        }
    }

private:
    std::uint8_t byte_of(char high, char low) const {
        const auto digit = [&](char c) -> unsigned {
            if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
            throw AssemblyError(number_, std::string("invalid hex digit '") + c + "'");
        };
        return static_cast<std::uint8_t>(digit(high) << 4 | digit(low));
    }

    std::vector<std::string> tokens_;
    std::vector<std::size_t> strings_;
    std::size_t position_ = 0;
    std::size_t number_;
};

std::vector<nvr::Assignment> assignments(Tokens& in) {
    in.keyword("assign");
    return in.list([&] {
        nvr::Assignment assignment;
        assignment.slot = in.index("assigned slot");
        in.keyword("=");
        assignment.value = in.value();
        return assignment;
    });
}

} // namespace

std::string disassemble(const nvr::Image& image) {
    const auto minor = std::max(image.minor_version, nvr::required_minor_version(image));
    std::ostringstream out;
    out << "; Neura assembly: the lossless text form of an NVR image.\n";
    out << "; `neura assemble` turns it back into the same image.\n";
    out << "nvr 1." << minor << " profile "
        << nvr::graph_profile << "\n";
    out << "entry " << image.entry << "\n\n";
    for (std::size_t i = 0; i < image.types.size(); ++i) {
        const auto& type = image.types[i];
        out << "type " << i << " " << type_kinds.at(static_cast<std::size_t>(type.kind));
        switch (type.kind) {
        case nvr::TypeKind::Unsigned:
        case nvr::TypeKind::Signed:
        case nvr::TypeKind::Float:
        case nvr::TypeKind::BFloat: out << " " << static_cast<unsigned>(type.width); break;
        case nvr::TypeKind::Object:
        case nvr::TypeKind::Array:
        case nvr::TypeKind::List: out << " " << type.reference; break;
        default: break;
        }
        out << "\n";
    }
    for (std::size_t i = 0; i < image.layouts.size(); ++i) {
        out << "layout " << i << " parent " << index_or_absent(image.layouts[i].parent) << " fields "
            << indices(image.layouts[i].fields);
        if (minor >= 2)
            out << " interfaces " << indices(image.layouts[i].interfaces);
        out << "\n";
    }
    for (std::size_t i = 0; i < image.constants.size(); ++i) {
        const auto& constant = image.constants[i];
        out << "constant " << i << " type " << constant.type << " bytes " << hex(constant.payload);
        if (const auto comment = constant_comment(image, constant); !comment.empty()) {
            out << " ; " << comment;
        }
        out << "\n";
    }
    for (std::size_t i = 0; i < image.globals.size(); ++i) {
        out << "global " << i << " type " << image.globals[i].type << " initializer "
            << index_or_absent(image.globals[i].initializer) << "\n";
    }
    for (std::size_t g = 0; g < image.graphs.size(); ++g) {
        const auto& graph = image.graphs[g];
        out << "\ngraph " << g << " inputs " << graph.input_lanes << " outputs " << indices(graph.outputs) << "\n";
        for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
            const auto& node = graph.nodes[n];
            out << "  node " << n << " " << (node.activation == nvr::Activation::Relu ? "relu" : "identity") << " "
                << node.bias << " "
                << list(node.edges, [](const nvr::Edge& edge) {
                       return std::to_string(edge.source) + "*" + std::to_string(edge.weight);
                   })
                << "\n";
        }
    }
    out << "\n";
    for (std::size_t i = 0; i < image.imports.size(); ++i) {
        const auto& import = image.imports[i];
        out << "import " << i << " " << quoted(import.name) << " version " << import.interface_version
            << " parameters " << indices(import.parameter_types) << " success " << import.success_type << " error "
            << import.error_type << "\n";
    }
    for (std::size_t f = 0; f < image.functions.size(); ++f) {
        const auto& function = image.functions[f];
        out << "\nfunction " << f << " parameters " << function.parameter_count << " result "
            << function.result_type << " frame " << indices(function.frame_types) << "\n";
        for (std::size_t b = 0; b < function.blocks.size(); ++b) {
            const auto& block = function.blocks[b];
            out << "  block " << b << " graph " << block.graph << " selector " << block.selector_start
                << " inputs " << values(block.inputs) << "\n";
            for (const auto& exit : block.exits) {
                out << "    exit " << exit_kinds.at(static_cast<std::size_t>(exit.kind));
                switch (exit.kind) {
                case nvr::ExitKind::Continue:
                    out << " resume " << exit.resume;
                    break;
                case nvr::ExitKind::Call:
                    out << " target " << exit.target << " arguments " << values(exit.arguments) << " result "
                        << index_or_absent(exit.result_slot) << " resume " << exit.resume;
                    break;
                case nvr::ExitKind::Return:
                    out << " arguments " << values(exit.arguments);
                    break;
                case nvr::ExitKind::State:
                    out << " " << state_ops.at(static_cast<std::size_t>(exit.operation)) << " immediate "
                        << exit.immediate << " arguments " << values(exit.arguments) << " result "
                        << index_or_absent(exit.result_slot) << " resume " << exit.resume;
                    break;
                case nvr::ExitKind::Host:
                    out << " import " << exit.target << " arguments " << values(exit.arguments) << " result "
                        << index_or_absent(exit.result_slot) << " resume " << exit.resume << " error "
                        << index_or_absent(exit.error_slot) << " error-resume " << exit.error_resume;
                    break;
                case nvr::ExitKind::Trap:
                    out << " reason " << exit.reason;
                    break;
                case nvr::ExitKind::MessageTrap:
                    out << " arguments " << values(exit.arguments);
                    break;
                }
                out << " assign "
                    << list(exit.assignments, [](const nvr::Assignment& assignment) {
                           return std::to_string(assignment.slot) + " = " + value(assignment.value);
                       })
                    << "\n";
            }
        }
    }
    if (!image.callbacks.empty()) {
        out << "\n";
        for (std::size_t i = 0; i < image.callbacks.size(); ++i) {
            const auto& callback = image.callbacks[i];
            out << "callback " << i << " " << quoted(callback.name) << " version " << callback.interface_version
                << " function " << callback.function << "\n";
        }
    }
    if (image.native_library) {
        const auto& library = *image.native_library;
        out << "\nnative-library " << quoted(library.triple) << " " << hex(library.object) << "\n";
        for (const auto& symbol : library.symbols) {
            out << "native-symbol " << quoted(symbol) << "\n";
        }
    }
    if (image.source_map) {
        out << "\nsource-map\n";
        for (const auto& location : *image.source_map) {
            out << "location " << location.function << " " << location.block << " " << quoted(location.path) << " "
                << location.line << " " << location.column << "\n";
        }
    }
    if (!image.other_sections.empty()) {
        out << "\n";
        for (const auto& section : image.other_sections) {
            out << "section " << quoted(std::string(section.tag.begin(), section.tag.end())) << " "
                << hex(section.data) << "\n";
        }
    }
    return out.str();
}

nvr::Image assemble(std::string_view text) {
    nvr::Image image;
    bool header = false;
    bool entry = false;
    std::size_t number = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++number;
        Tokens in(line, number);
        if (in.empty()) {
            continue;
        }
        const auto record = in.next("record");
        if (!header && record != "nvr") {
            in.error("the text must start with an 'nvr' header");
        }
        if (record == "nvr") {
            if (header) {
                in.error("a second 'nvr' header");
            }
            header = true;
            const auto version = in.next("format version");
            if (version.size() != 3 || !version.starts_with("1.") ||
                version[2] < '0' || version[2] - '0' > nvr::minor_version) {
                in.error("NVR " + version + " is not supported; this assembler writes 1.0 through 1." +
                         std::to_string(nvr::minor_version));
            }
            image.minor_version = static_cast<std::uint16_t>(version.back() - '0');
            in.keyword("profile");
            if (in.index("graph profile") != nvr::graph_profile) {
                in.error("only graph profile 1 is supported");
            }
        } else if (record == "entry") {
            if (entry) {
                in.error("a second entry");
            }
            entry = true;
            image.entry = in.index("entry function");
        } else if (record == "type") {
            in.expect_index(static_cast<std::uint32_t>(image.types.size()), "type");
            nvr::Type type;
            type.kind = static_cast<nvr::TypeKind>(in.choice(type_kinds, "type kind"));
            switch (type.kind) {
            case nvr::TypeKind::Unsigned:
            case nvr::TypeKind::Signed:
            case nvr::TypeKind::Float:
            case nvr::TypeKind::BFloat: type.width = in.number<std::uint8_t>("width"); break;
            case nvr::TypeKind::Object:
            case nvr::TypeKind::Array:
            case nvr::TypeKind::List: type.reference = in.index("type reference"); break;
            default: break;
            }
            image.types.push_back(type);
        } else if (record == "layout") {
            in.expect_index(static_cast<std::uint32_t>(image.layouts.size()), "layout");
            nvr::Layout layout;
            in.keyword("parent");
            layout.parent = in.index_or_absent("parent layout");
            in.keyword("fields");
            layout.fields = in.indices("field type");
            if (image.minor_version >= 2) {
                in.keyword("interfaces");
                layout.interfaces = in.indices("interface layout");
            }
            image.layouts.push_back(std::move(layout));
        } else if (record == "constant") {
            in.expect_index(static_cast<std::uint32_t>(image.constants.size()), "constant");
            nvr::Constant constant;
            in.keyword("type");
            constant.type = in.index("constant type");
            in.keyword("bytes");
            constant.payload = in.bytes();
            image.constants.push_back(std::move(constant));
        } else if (record == "global") {
            in.expect_index(static_cast<std::uint32_t>(image.globals.size()), "global");
            nvr::Global global;
            in.keyword("type");
            global.type = in.index("global type");
            in.keyword("initializer");
            global.initializer = in.index_or_absent("initializer constant");
            image.globals.push_back(global);
        } else if (record == "graph") {
            in.expect_index(static_cast<std::uint32_t>(image.graphs.size()), "graph");
            nvr::Graph graph;
            in.keyword("inputs");
            graph.input_lanes = in.index("input lane count");
            in.keyword("outputs");
            graph.outputs = in.indices("output wire");
            image.graphs.push_back(std::move(graph));
        } else if (record == "node") {
            if (image.graphs.empty()) {
                in.error("a node before any graph");
            }
            auto& graph = image.graphs.back();
            in.expect_index(static_cast<std::uint32_t>(graph.nodes.size()), "node");
            nvr::Node node;
            const auto activation = in.next("activation");
            if (activation == "relu") {
                node.activation = nvr::Activation::Relu;
            } else if (activation != "identity") {
                in.error("unknown activation '" + activation + "'");
            }
            node.bias = in.number<std::int64_t>("bias");
            node.edges = in.list([&] {
                const auto token = in.next("edge");
                const auto star = token.find('*');
                if (star == std::string::npos) {
                    in.error("expected an edge of the form source*weight, found '" + token + "'");
                }
                nvr::Edge edge;
                const auto source = std::string_view(token).substr(0, star);
                const auto weight = std::string_view(token).substr(star + 1);
                const auto [source_end, source_failure] =
                    std::from_chars(source.data(), source.data() + source.size(), edge.source);
                const auto [weight_end, weight_failure] =
                    std::from_chars(weight.data(), weight.data() + weight.size(), edge.weight);
                if (source_failure != std::errc{} || weight_failure != std::errc{} ||
                    source_end != source.data() + source.size() || weight_end != weight.data() + weight.size()) {
                    in.error("malformed edge '" + token + "'");
                }
                return edge;
            });
            graph.nodes.push_back(std::move(node));
        } else if (record == "import") {
            in.expect_index(static_cast<std::uint32_t>(image.imports.size()), "import");
            nvr::Import import;
            import.name = in.string("import name");
            in.keyword("version");
            import.interface_version = in.index("interface version");
            in.keyword("parameters");
            import.parameter_types = in.indices("parameter type");
            in.keyword("success");
            import.success_type = in.index("success type");
            in.keyword("error");
            import.error_type = in.index("error type");
            image.imports.push_back(std::move(import));
        } else if (record == "function") {
            in.expect_index(static_cast<std::uint32_t>(image.functions.size()), "function");
            nvr::Function function;
            in.keyword("parameters");
            function.parameter_count = in.index("parameter count");
            in.keyword("result");
            function.result_type = in.index("result type");
            in.keyword("frame");
            function.frame_types = in.indices("frame slot type");
            image.functions.push_back(std::move(function));
        } else if (record == "block") {
            if (image.functions.empty()) {
                in.error("a block before any function");
            }
            auto& function = image.functions.back();
            in.expect_index(static_cast<std::uint32_t>(function.blocks.size()), "block");
            nvr::Block block;
            in.keyword("graph");
            block.graph = in.index("graph");
            in.keyword("selector");
            block.selector_start = in.index("selector start");
            in.keyword("inputs");
            block.inputs = in.list([&] { return in.value(); });
            function.blocks.push_back(std::move(block));
        } else if (record == "exit") {
            if (image.functions.empty() || image.functions.back().blocks.empty()) {
                in.error("an exit before any block");
            }
            nvr::Exit exit;
            exit.kind = static_cast<nvr::ExitKind>(in.choice(exit_kinds, "exit kind"));
            const auto arguments = [&] {
                in.keyword("arguments");
                exit.arguments = in.list([&] { return in.value(); });
            };
            const auto result = [&] {
                in.keyword("result");
                exit.result_slot = in.index_or_absent("result slot");
            };
            const auto resume = [&] {
                in.keyword("resume");
                exit.resume = in.index("resume block");
            };
            switch (exit.kind) {
            case nvr::ExitKind::Continue:
                resume();
                break;
            case nvr::ExitKind::Call:
                in.keyword("target");
                exit.target = in.index("target function");
                arguments();
                result();
                resume();
                break;
            case nvr::ExitKind::Return:
                arguments();
                break;
            case nvr::ExitKind::State:
                exit.operation = static_cast<nvr::StateOp>(in.choice(state_ops, "state operation"));
                in.keyword("immediate");
                exit.immediate = in.index("immediate");
                arguments();
                result();
                resume();
                break;
            case nvr::ExitKind::Host:
                in.keyword("import");
                exit.target = in.index("import");
                arguments();
                result();
                resume();
                in.keyword("error");
                exit.error_slot = in.index_or_absent("error slot");
                in.keyword("error-resume");
                exit.error_resume = in.index("error block");
                break;
            case nvr::ExitKind::Trap:
                in.keyword("reason");
                exit.reason = in.index("trap reason");
                break;
            case nvr::ExitKind::MessageTrap:
                arguments();
                break;
            }
            exit.assignments = assignments(in);
            image.functions.back().blocks.back().exits.push_back(std::move(exit));
        } else if (record == "callback") {
            in.expect_index(static_cast<std::uint32_t>(image.callbacks.size()), "callback");
            nvr::Callback callback;
            callback.name = in.string("callback name");
            in.keyword("version");
            callback.interface_version = in.index("interface version");
            in.keyword("function");
            callback.function = in.index("function");
            image.callbacks.push_back(std::move(callback));
        } else if (record == "native-library") {
            if (image.native_library) {
                in.error("a second native library");
            }
            nvr::NativeLibrary library;
            library.triple = in.string("target triple");
            library.object = in.bytes();
            image.native_library = std::move(library);
        } else if (record == "native-symbol") {
            if (!image.native_library) {
                in.error("a native symbol before 'native-library'");
            }
            image.native_library->symbols.push_back(in.string("native symbol"));
        } else if (record == "source-map") {
            if (image.source_map) {
                in.error("a second source map");
            }
            image.source_map.emplace();
        } else if (record == "location") {
            if (!image.source_map) {
                in.error("a location before 'source-map'");
            }
            nvr::SourceLocation location;
            location.function = in.index("function");
            location.block = in.index("block");
            location.path = in.string("path");
            location.line = in.index("line");
            location.column = in.index("column");
            image.source_map->push_back(std::move(location));
        } else if (record == "section") {
            const auto tag = in.string("section tag");
            if (tag.size() != 4) {
                in.error("a section tag has four characters");
            }
            nvr::RawSection section;
            std::copy(tag.begin(), tag.end(), section.tag.begin());
            section.data = in.bytes();
            image.other_sections.push_back(std::move(section));
        } else {
            in.error("unknown record '" + record + "'");
        }
        in.finish();
    }
    if (!header) {
        throw AssemblyError(number, "the text has no 'nvr' header");
    }
    if (!entry) {
        throw AssemblyError(number, "the text has no entry");
    }
    return image;
}

} // namespace neura::decompiler
