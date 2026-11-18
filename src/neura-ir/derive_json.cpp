#include "derive_json.hpp"

#include <algorithm>
#include <sstream>

namespace neura {
namespace {

std::string literal(std::string_view value) {
    std::string out = "\"";
    for (const char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out + "\"";
}

std::string member_path(const std::string& name) {
    const auto first = [](char c) { return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (!name.empty() && first(name.front()) && std::ranges::all_of(name, [&](char c) {
            return first(c) || (c >= '0' && c <= '9');
        })) return "." + name;
    return "[" + literal(name) + "]";
}

class CodecSource {
public:
    explicit CodecSource(const std::vector<CodecShape>& shapes) : shapes_(shapes) {}

    std::string module(std::string_view name) {
        const auto& root = shapes_.front().spelling;
        out_ << "let " << name << " = module (\n"
             << "    export let to_json = func(value: " << root
             << ") -> CodecResult<CodecJson> => encode0(value, \"$\");\n"
             << "    export let encode = func(value: " << root
             << ") -> CodecResult<string> => CodecResult<string>.Ok(CodecJsonModule.write(to_json(value)?));\n"
             << "    export let from_json = func(value: CodecJson) -> CodecResult<" << root
             << "> => decode0(value, \"$\");\n"
             << "    export let decode = func(text: string) -> CodecResult<" << root
             << "> => from_json(CodecSupport.parse(text)?);\n";
        for (std::size_t i = 0; i < shapes_.size(); ++i) {
            const auto& shape = shapes_[i];
            if (shape.type.kind != TypeKind::Enum && shape.type.kind != TypeKind::Object) continue;
            out_ << "    let encode" << i << " = func(value: " << shape.spelling
                 << ", path: string) -> CodecResult<CodecJson> {\n";
            encode(shape);
            out_ << "    };\n    let decode" << i << " = func(value: CodecJson, path: string) -> CodecResult<"
                 << shape.spelling << "> {\n";
            decode(shape);
            out_ << "    };\n";
        }
        out_ << ");\n";
        return out_.str();
    }

private:
    const std::vector<CodecShape>& shapes_;
    std::ostringstream out_;

    static std::string nth(std::size_t i) { return "v" + std::to_string(i); }
    static std::string suffix(const CodecSlot& slot, bool named, std::size_t i) {
        return named ? member_path(slot.wire) : "[" + std::to_string(i) + "]";
    }

    void error(const std::string& result, std::string_view code, const std::string& path = "path") {
        out_ << "return CodecResult<" << result << ">.Err(CodecError.custom(\"json.codec." << code << "\", " << path
             << " + " << literal(": " + std::string(code)) << "));";
    }

    static std::string list_type(bool named) { return named ? "[string, CodecJson][list]" : "CodecJson[list]"; }

    std::size_t temporary_{};

    // A scalar calls its trusted width-specific conversion. An aggregate
    // calls its schema helper. A tuple is unrolled in place, with one
    // evaluation of its source and an exact array arity check.
    std::string convert(std::size_t index, const std::string& value, const std::string& path,
                        const std::string& result, bool encoding) {
        const auto& shape = shapes_[index];
        if (shape.type.kind != TypeKind::Tuple) {
            const auto direction = std::string(encoding ? "encode" : "decode");
            const bool aggregate = shape.type.kind == TypeKind::Object || shape.type.kind == TypeKind::Enum;
            const auto callee = aggregate ? direction + std::to_string(index)
                                          : "CodecSupport." + direction + "_" + shape.spelling;
            return callee + "(" + value + ", " + path + ")?";
        }
        const auto number = std::to_string(temporary_++);
        const auto tuple = "tuple" + number;
        const auto array = "elements" + number;
        if (encoding) {
            out_ << "        last " << tuple << " = " << value << ";\n"
                 << "        last " << array << ": CodecJson[list] = new;\n";
        } else {
            out_ << "        last " << tuple << ": " << shape.spelling << ";\n"
                 << "        match (" << value << ") { CodecJson.Array(" << array << ") => {\n"
                 << "        if (" << array << ".length != " << shape.slots.size() << ") { ";
            error(result, "type", path);
            out_ << " }\n";
        }
        std::string decoded;
        for (std::size_t i = 0; i < shape.slots.size(); ++i) {
            const auto subscript = "[" + std::to_string(i) + "]";
            const auto child = convert(shape.slots[i].shape, (encoding ? tuple : array) + subscript,
                                       path + " + " + literal(subscript), result, encoding);
            if (encoding) out_ << "        " << array << ".append(" << child << ");\n";
            else {
                const auto name = tuple + "part" + std::to_string(i);
                out_ << "        last " << name << " = " << child << ";\n";
                if (!decoded.empty()) decoded += ", ";
                decoded += name;
            }
        }
        if (encoding) return "CodecJson.Array(" + array + ")";
        out_ << "        " << tuple << " = [" << decoded << "];\n        }, _ => { ";
        error(result, "type", path);
        out_ << " }, }\n";
        return tuple;
    }

    void encode_slots(const std::vector<CodecSlot>& slots, bool named, bool variants) {
        out_ << "        last items: " << list_type(named) << " = new;\n";
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const auto& s = slots[i];
            const auto value = variants ? nth(i) : "value." + s.name;
            const auto path = "path + " + literal((variants ? ".payload" : "") + suffix(s, named, i));
            if (s.optional) out_ << "        match (" << value << ") { CodecOptional.Some(present) => {\n";
            const auto element = convert(s.shape, s.optional ? "present" : value, path, "CodecJson", true);
            out_ << "        items.append(" << (named ? "[" + literal(s.wire) + ", " + element + "]" : element)
                 << ");\n";
            if (s.optional) out_ << "        }, CodecOptional.None => {}, }\n";
        }
    }

    static std::string variant_args(const CodecVariant& variant) {
        if (variant.slots.empty()) return {};
        std::string args = variant.named ? " { " : "(";
        for (std::size_t i = 0; i < variant.slots.size(); ++i) {
            if (i) args += ", ";
            if (variant.named) args += variant.slots[i].name + ": ";
            args += nth(i);
        }
        return args + (variant.named ? " }" : ")");
    }

    void encode(const CodecShape& s) {
        if (s.type.kind == TypeKind::Enum) {
            out_ << "        last object: [string, CodecJson][list] = new;\n        match (value) {\n";
            for (const auto& v : s.variants) {
                out_ << "        " << s.spelling.substr(0, s.spelling.find('<')) << "." << v.name << variant_args(v) << " => {\n"
                     << "        object.append([\"variant\", CodecJson.Text(" << literal(v.wire) << ")]);\n";
                if (!v.slots.empty()) {
                    encode_slots(v.slots, v.named, true);
                    out_ << "        object.append([\"payload\", CodecJson." << (v.named ? "Object" : "Array")
                         << "(items)]);\n";
                }
                out_ << "        },\n";
            }
            out_ << "        }\n        return CodecResult<CodecJson>.Ok(CodecJson.Object(object));\n";
        } else {
            out_ << "        if (value == nil) { return CodecResult<CodecJson>.Ok(CodecJson.Null); }\n";
            encode_slots(s.slots, true, false);
            out_ << "        return CodecResult<CodecJson>.Ok(CodecJson.Object(items));\n";
        }
    }

    // Check duplicates in input order before checking missing fields or decoding their values.
    void collect(const std::vector<CodecSlot>& slots, const std::string& result, const std::string& items,
                 const std::string& prefix) {
        for (std::size_t i = 0; i < slots.size(); ++i)
            out_ << "        let seen" << i << " = false; let raw" << i << " = CodecJson.Null;\n";
        out_ << "        for (let entry in " << items << ") {\n";
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const auto wire = literal(slots[i].wire);
            out_ << "        if (entry[0] == " << wire << ") { if (seen" << i << ") { ";
            error(result, "duplicate", "path + " + literal(prefix + member_path(slots[i].wire)));
            out_ << " } seen" << i << " = true; raw" << i << " = entry[1]; }\n";
        }
        out_ << "        }\n";
    }

    void decode_slots(const std::vector<CodecSlot>& slots, bool named, const std::string& result,
                      const std::string& prefix) {
        if (named) collect(slots, result, "items", prefix);
        else {
            out_ << "        if (items.length != " << slots.size() << ") { ";
            error(result, prefix.empty() ? "type" : "payload", "path + " + literal(prefix));
            out_ << " }\n";
        }
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const auto& slot = slots[i];
            const auto number = std::to_string(i);
            const auto place = "path + " + literal(prefix + suffix(slot, named, i));
            if (named && !slot.optional) {
                out_ << "        if (!seen" << i << ") { ";
                error(result, "missing", place);
                out_ << " }\n";
            }
            const auto value = named ? "raw" + number : "items[" + number + "]";
            const auto opt = "CodecOptional<" + shapes_[slot.shape].spelling + ">";
            if (slot.optional)
                out_ << "        last " << nth(i) << ": " << opt << "; if (seen" << i << ") {\n";
            const auto expr = convert(slot.shape, value, place, result, false);
            if (slot.optional)
                out_ << "        " << nth(i) << " = " << opt << ".Some(" << expr << "); } else { "
                     << nth(i) << " = " << opt << ".None; }\n";
            else out_ << "        last " << nth(i) << " = " << expr << ";\n";
        }
    }

    void decode(const CodecShape& s) {
        const auto& result = s.spelling;
        if (s.type.kind == TypeKind::Enum) {
            out_ << "        match (value) { CodecJson.Object(object) => {\n";
            collect({{no_symbol, "variant", "variant", 0, false}, {no_symbol, "payload", "payload", 0, false}},
                    result, "object", "");
            out_ << "        if (!seen0) { "; error(result, "missing", "path + \".variant\""); out_ << " }\n";
            out_ << "        match (raw0) { CodecJson.Text(tag) => {\n";
            for (const auto& v : s.variants) {
                out_ << "        if (tag == " << literal(v.wire) << ") {\n";
                const auto target = result + "." + v.name;
                if (v.slots.empty()) {
                    out_ << "        if (seen1) { "; error(result, "payload", "path + \".payload\""); out_ << " }\n"
                         << "        return CodecResult<" << result << ">.Ok(" << target << ");\n";
                } else {
                    out_ << "        if (!seen1) { "; error(result, "payload", "path + \".payload\""); out_ << " }\n"
                         << "        match (raw1) { CodecJson." << (v.named ? "Object" : "Array") << "(items) => {\n";
                    decode_slots(v.slots, v.named, result, ".payload");
                    out_ << "        return CodecResult<" << result << ">.Ok(" << target << variant_args(v)
                         << ");\n        }, _ => { "; error(result, "payload", "path + \".payload\""); out_ << " }, }\n";
                }
                out_ << "        }\n";
            }
            error(result, "variant", "path + \".variant\""); out_ << "\n        }, _ => { ";
            error(result, "type", "path + \".variant\""); out_ << " }, }\n        }, _ => { ";
            error(result, "type"); out_ << " }, }\n";
        } else {
            out_ << "        match (value) {\n"
                 << "        CodecJson.Null => { return CodecResult<" << result << ">.Ok(nil); },\n"
                 << "        CodecJson.Object(items) => {\n";
            decode_slots(s.slots, true, result, "");
            out_ << "        return CodecResult<" << result << ">.Ok(new " << result << "(";
            for (std::size_t i = 0; i < s.slots.size(); ++i) {
                if (i) out_ << ", ";
                out_ << nth(i);
            }
            out_ << "));\n        }, _ => { ";
            error(result, "type");
            out_ << " }, }\n";
        }
    }
};
}

std::string json_codec_declaration(std::string_view name, const std::vector<CodecShape>& shapes) {
    return CodecSource(shapes).module(name);
}
}
