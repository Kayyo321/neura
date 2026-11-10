#include "format.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace neura::nvr {

unsigned lane_count(const Type& type) {
    switch (type.kind) {
    case TypeKind::Void: return 0;
    case TypeKind::Bool: return 1;
    case TypeKind::Unsigned:
    case TypeKind::Signed:
    case TypeKind::Float:
    case TypeKind::BFloat: return type.width;
    case TypeKind::Text:
    case TypeKind::Bytes:
    case TypeKind::Object:
    case TypeKind::Array:
    case TypeKind::List: return 64;
    }
    return 0;
}

unsigned selector_lanes(std::size_t exit_count) {
    return exit_count <= 1 ? 0U : static_cast<unsigned>(std::bit_width(exit_count - 1));
}

bool is_reference(TypeKind kind) {
    return kind == TypeKind::Text || kind == TypeKind::Bytes || kind == TypeKind::Object ||
           kind == TypeKind::Array || kind == TypeKind::List;
}

namespace {

class Output {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(std::uint16_t value) { little_endian(value, 2); }
    void u32(std::uint32_t value) { little_endian(value, 4); }
    void u64(std::uint64_t value) { little_endian(value, 8); }
    void i64(std::int64_t value) { little_endian(static_cast<std::uint64_t>(value), 8); }

    void count(std::size_t value) {
        if (value >= absent) {
            throw std::invalid_argument("NVR vector has too many elements");
        }
        u32(static_cast<std::uint32_t>(value));
    }

    void bytes(std::span<const std::uint8_t> data) {
        count(data.size());
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }

    void text(std::string_view data) {
        count(data.size());
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }

    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    void little_endian(std::uint64_t value, int width) {
        for (int i = 0; i < width; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }

    std::vector<std::uint8_t> bytes_;
};

template <typename T, typename Write>
void vector(Output& out, const std::vector<T>& items, Write write) {
    out.count(items.size());
    for (const auto& item : items) {
        write(out, item);
    }
}

void indices(Output& out, const std::vector<std::uint32_t>& items) {
    vector(out, items, [](Output& o, std::uint32_t index) { o.u32(index); });
}

void value(Output& out, const Value& item) {
    out.u8(static_cast<std::uint8_t>(item.kind));
    out.u32(item.type);
    out.u32(item.index);
}

void values(Output& out, const std::vector<Value>& items) {
    vector(out, items, value);
}

void exit(Output& out, const Exit& item, bool returns_value) {
    out.u8(static_cast<std::uint8_t>(item.kind));
    vector(out, item.assignments, [](Output& o, const Assignment& assignment) {
        o.u32(assignment.slot);
        value(o, assignment.value);
    });
    switch (item.kind) {
    case ExitKind::Continue:
        out.u32(item.resume);
        break;
    case ExitKind::Call:
        out.u32(item.target);
        values(out, item.arguments);
        out.u32(item.result_slot);
        out.u32(item.resume);
        break;
    case ExitKind::Return:
        if (returns_value != (item.arguments.size() == 1) || item.arguments.size() > 1) {
            throw std::invalid_argument("return exit value does not match the function result");
        }
        if (returns_value) {
            value(out, item.arguments.front());
        }
        break;
    case ExitKind::State:
        out.u8(static_cast<std::uint8_t>(item.operation));
        out.u32(item.immediate);
        values(out, item.arguments);
        out.u32(item.result_slot);
        out.u32(item.resume);
        break;
    case ExitKind::Host:
        out.u32(item.target);
        values(out, item.arguments);
        out.u32(item.result_slot);
        out.u32(item.resume);
        out.u32(item.error_slot);
        out.u32(item.error_resume);
        break;
    case ExitKind::Trap:
        out.u32(item.reason);
        break;
    case ExitKind::MessageTrap:
        values(out, item.arguments);
        break;
    }
}

struct Section {
    std::array<char, 4> tag;
    bool required;
    std::vector<std::uint8_t> data;
};

std::vector<Section> sections(const Image& image, std::uint16_t minor) {
    std::vector<Section> result;
    auto add = [&](const char* tag, bool required, auto write) {
        Output out;
        write(out);
        result.push_back({{tag[0], tag[1], tag[2], tag[3]}, required, out.take()});
    };

    // Sections in ascending tag order.
    add("CONS", true, [&](Output& out) {
        vector(out, image.constants, [](Output& o, const Constant& constant) {
            o.u32(constant.type);
            o.bytes(constant.payload);
        });
    });
    add("FUNC", true, [&](Output& out) {
        vector(out, image.functions, [&](Output& o, const Function& function) {
            if (function.result_type >= image.types.size()) {
                throw std::invalid_argument("function result type is out of range");
            }
            const bool returns_value = image.types[function.result_type].kind != TypeKind::Void;
            o.u32(function.parameter_count);
            o.u32(function.result_type);
            indices(o, function.frame_types);
            vector(o, function.blocks, [&](Output& b, const Block& block) {
                b.u32(block.graph);
                values(b, block.inputs);
                b.u32(block.selector_start);
                vector(b, block.exits, [&](Output& e, const Exit& item) { exit(e, item, returns_value); });
            });
        });
    });
    add("GLOB", true, [&](Output& out) {
        vector(out, image.globals, [](Output& o, const Global& global) {
            o.u32(global.type);
            o.u32(global.initializer);
        });
    });
    add("GRPH", true, [&](Output& out) {
        vector(out, image.graphs, [](Output& o, const Graph& graph) {
            o.u32(graph.input_lanes);
            vector(o, graph.nodes, [](Output& n, const Node& node) {
                n.u8(static_cast<std::uint8_t>(node.activation));
                n.i64(node.bias);
                vector(n, node.edges, [](Output& e, const Edge& edge) {
                    e.u32(edge.source);
                    e.i64(edge.weight);
                });
            });
            indices(o, graph.outputs);
        });
    });
    if (!image.callbacks.empty()) {
        add("CALB", true, [&](Output& out) {
            vector(out, image.callbacks, [](Output& o, const Callback& callback) {
                o.text(callback.name);
                o.u32(callback.interface_version);
                o.u32(callback.function);
            });
        });
    }
    if (image.native_library) {
        add("NATV", true, [&](Output& out) {
            out.text(image.native_library->triple);
            out.bytes(image.native_library->object);
            vector(out, image.native_library->symbols, [](Output& o, const std::string& symbol) { o.text(symbol); });
        });
    }
    add("IMPT", true, [&](Output& out) {
        vector(out, image.imports, [](Output& o, const Import& import) {
            o.text(import.name);
            o.u32(import.interface_version);
            indices(o, import.parameter_types);
            o.u32(import.success_type);
            o.u32(import.error_type);
        });
    });
    add("LAYO", true, [&](Output& out) {
        vector(out, image.layouts, [&](Output& o, const Layout& layout) {
            o.u32(layout.parent);
            indices(o, layout.fields);
            if (minor >= 2) indices(o, layout.interfaces);
        });
    });
    add("META", true, [&](Output& out) { out.u32(image.entry); });
    if (image.source_map) {
        add("SMAP", false, [&](Output& out) {
            vector(out, *image.source_map, [](Output& o, const SourceLocation& location) {
                o.u32(location.function);
                o.u32(location.block);
                o.text(location.path);
                o.u32(location.line);
                o.u32(location.column);
            });
        });
    }
    for (const auto& raw : image.other_sections) {
        for (char c : raw.tag) {
            if (c < 0x20 || c > 0x7e) {
                throw std::invalid_argument("an optional section tag is not printable ASCII");
            }
        }
        result.push_back({raw.tag, false, raw.data});
    }
    add("TYPE", true, [&](Output& out) {
        vector(out, image.types, [](Output& o, const Type& type) {
            o.u8(static_cast<std::uint8_t>(type.kind));
            switch (type.kind) {
            case TypeKind::Unsigned:
            case TypeKind::Signed:
            case TypeKind::Float:
            case TypeKind::BFloat: o.u8(type.width); break;
            case TypeKind::Object:
            case TypeKind::Array:
            case TypeKind::List: o.u32(type.reference); break;
            default: break;
            }
        });
    });
    std::stable_sort(result.begin(), result.end(),
                     [](const Section& a, const Section& b) { return a.tag < b.tag; });
    for (std::size_t i = 1; i < result.size(); ++i) {
        if (result[i - 1].tag == result[i].tag) {
            throw std::invalid_argument("two sections share the tag " +
                                        std::string(result[i].tag.begin(), result[i].tag.end()));
        }
    }
    return result;
}

} // namespace

std::uint16_t required_minor_version(const Image& image) {
    if (!image.callbacks.empty() || image.native_library) return 4;
    std::uint16_t version = 0;
    if (std::ranges::any_of(image.layouts, [](const Layout& layout) { return !layout.interfaces.empty(); }))
        version = 2;
    if (std::ranges::any_of(image.constants, [&](const Constant& value) {
            return value.type < image.types.size() && image.types[value.type].kind == TypeKind::Object;
        })) {
        version = 2;
    }
    for (const auto& type : image.types) {
        if (type.kind == TypeKind::Float || type.kind == TypeKind::BFloat) {
            version = std::max<std::uint16_t>(version, 1);
            break;
        }
    }
    for (const auto& function : image.functions) {
        for (const auto& block : function.blocks) {
            for (const auto& exit : block.exits) {
                if (exit.kind == ExitKind::State && exit.operation == StateOp::RepeatCollection) return 3;
                if (exit.kind == ExitKind::MessageTrap ||
                    (exit.kind == ExitKind::State && exit.operation >= StateOp::TextSlice)) {
                    version = 2;
                }
                if ((exit.kind == ExitKind::State && exit.operation == StateOp::ReadVersion) ||
                    (exit.kind == ExitKind::Trap && exit.reason == static_cast<std::uint32_t>(
                                                                      TrapReason::ModifiedDuringIteration))) {
                    version = std::max<std::uint16_t>(version, 1);
                }
            }
        }
    }
    return version;
}

std::vector<std::uint8_t> encode(const Image& image) {
    if (image.minor_version > minor_version) {
        throw std::invalid_argument("the image records an unsupported minor version");
    }
    const auto minor = std::max(image.minor_version, required_minor_version(image));
    const auto parts = sections(image, minor);
    constexpr std::uint64_t header_size = 24;
    constexpr std::uint64_t directory_record_size = 24;
    std::uint64_t offset = header_size + directory_record_size * parts.size();
    std::uint64_t file_size = offset;
    for (const auto& part : parts) {
        file_size += part.data.size();
    }

    Output out;
    for (char c : std::string_view("NEUR")) {
        out.u8(static_cast<std::uint8_t>(c));
    }
    out.u16(major_version);
    out.u16(minor);
    out.u32(graph_profile);
    out.u32(static_cast<std::uint32_t>(parts.size()));
    out.u64(file_size);
    for (const auto& part : parts) {
        for (char c : part.tag) {
            out.u8(static_cast<std::uint8_t>(c));
        }
        out.u32(part.required ? 1U : 0U);
        out.u64(offset);
        out.u64(part.data.size());
        offset += part.data.size();
    }
    auto bytes = out.take();
    bytes.reserve(file_size);
    for (const auto& part : parts) {
        bytes.insert(bytes.end(), part.data.begin(), part.data.end());
    }
    return bytes;
}

} // namespace neura::nvr
