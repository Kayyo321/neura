#include "c_import.hpp"

#include "libclang_loader.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>

namespace neura {
namespace {

namespace fs = std::filesystem;

// Names a Neura program cannot use for a declaration: the keywords and the
// built-in type names. A C name that collides gets a trailing underscore.
constexpr auto reserved = std::to_array<std::string_view>({
    "let", "const", "export", "override", "func", "module", "class", "import", "new", "destroy", "if", "else",
    "while", "do", "for", "in", "break", "continue", "return", "true", "false", "as", "is", "nil", "enum",
    "match", "and", "or", "interface", "implements", "promote", "static", "singleton", "this", "int", "int8",
    "int16", "int32", "int64", "uint", "uint8", "uint16", "uint32", "uint64", "float16", "bfloat16",
    "float32", "float64", "float", "bool", "string", "void", "unit", "host", "callback", "init",
});

std::string neura_name(const std::string& name) {
    return std::ranges::find(reserved, name) != reserved.end() ? name + "_" : name;
}

// The process's libclang, loaded on the first C import.
NeuraLibclang& libclang(const CImportOptions& options, const Span& site) {
    static std::unique_ptr<NeuraLibclang> loaded;
    static std::string failure;
    if (loaded) return *loaded;
    auto library = std::make_unique<NeuraLibclang>();
    if (!neura_libclang_load(library.get(), options.libclang.empty() ? nullptr : options.libclang.c_str())) {
        failure = library->error;
        throw Diagnostic(site, "C imports need libclang, which could not be loaded: " + failure +
                                   "; install libclang (for example the libclang1-19 package), or name the "
                                   "library with --libclang or the NEURA_LIBCLANG environment variable");
    }
    loaded = std::move(library);
    return *loaded;
}

class Importer {
public:
    Importer(NeuraLibclang& lib, fs::path header, bool linked, std::string module_name, const CImportOptions& options,
             const Span& site)
        : lib_(lib), header_(std::move(header)), linked_(linked), module_name_(std::move(module_name)),
          options_(options), site_(site) {}

    CImportResult run() {
        arguments_ = {"-x", "c", "-std=gnu11", "-I" + header_.parent_path().string()};
        for (const auto& dir : options_.include_dirs) arguments_.push_back("-I" + dir);
        for (const auto& define : options_.defines) arguments_.push_back("-D" + define);
        if (const auto resource = resource_dir(); !resource.empty()) {
            arguments_.push_back("-resource-dir");
            arguments_.push_back(resource);
        }
        index_ = lib_.clang_createIndex(0, 0);
        unit_ = parse(header_.string(), nullptr);
        const auto root = lib_.clang_getTranslationUnitCursor(unit_);
        // First pass: the typedef names of records, preferred over tags.
        visit(root, [&](CXCursor cursor) {
            if (lib_.clang_getCursorKind(cursor) == CXCursor_TypedefDecl && in_header(cursor)) {
                const auto underlying = lib_.clang_getCanonicalType(lib_.clang_getTypedefDeclUnderlyingType(cursor));
                if (underlying.kind == CXType_Record) {
                    const auto declaration = lib_.clang_getTypeDeclaration(underlying);
                    typedef_names_.emplace(key(declaration), text(lib_.clang_getCursorSpelling(cursor)));
                }
            }
        });
        visit(root, [&](CXCursor cursor) { declaration(cursor); });
        macros();
        lib_.clang_disposeTranslationUnit(unit_);
        lib_.clang_disposeIndex(index_);
        return finish();
    }

private:
    struct Scalar {
        std::string carrier;  // A ValueLayout constant and MemoryLayout.Carrier variant.
        std::string type;     // The Neura type.
        long long size = 0;
        long long align = 0;
    };

    struct Mapped {
        enum class Kind { Void, Scalar, Pointer, Struct, Unsupported } kind = Kind::Unsupported;
        Scalar scalar;
        std::string record;  // A struct's Neura class name.
        std::string reason;
        bool c_string = false;
    };

    struct Field {
        std::string name;
        Mapped type;
        long long offset = 0;
        long long size = 0;
        long long count = -1;  // The element count of an array field, or -1.
    };

    struct Record {
        std::string name;
        bool supported = false;
        std::string reason;
        std::vector<Field> fields;
        long long size = 0;
    };

    // --- libclang helpers ---------------------------------------------------

    std::string text(CXString string) {
        const char* chars = lib_.clang_getCString(string);
        std::string result = chars == nullptr ? std::string{} : std::string(chars);
        lib_.clang_disposeString(string);
        return result;
    }

    template <typename Visit>
    void visit(CXCursor parent, Visit visit_child) {
        lib_.clang_visitChildren(
            parent,
            [](CXCursor cursor, CXCursor, CXClientData data) {
                (*static_cast<Visit*>(data))(cursor);
                return CXChildVisit_Continue;
            },
            &visit_child);
    }

    std::string file_of(CXCursor cursor) {
        CXFile file = nullptr;
        unsigned line = 0, column = 0, offset = 0;
        lib_.clang_getSpellingLocation(lib_.clang_getCursorLocation(cursor), &file, &line, &column, &offset);
        if (file == nullptr) return {};
        return text(lib_.clang_getFileName(file));
    }

    bool in_header(CXCursor cursor) {
        const auto file = file_of(cursor);
        if (file.empty()) return false;
        std::error_code ignored;
        return fs::weakly_canonical(file, ignored) == fs::weakly_canonical(header_, ignored);
    }

    std::string key(CXCursor cursor) {
        CXFile file = nullptr;
        unsigned line = 0, column = 0, offset = 0;
        lib_.clang_getSpellingLocation(lib_.clang_getCursorLocation(cursor), &file, &line, &column, &offset);
        return (file == nullptr ? std::string{} : text(lib_.clang_getFileName(file))) + ":" + std::to_string(offset) +
               ":" + text(lib_.clang_getCursorSpelling(cursor));
    }

    std::string resource_dir() const {
        // libclang finds its builtin headers (stddef.h, stdarg.h) relative to
        // where it is installed; Debian's layout needs the directory named.
        const auto major = std::to_string(lib_.major);
        std::vector<fs::path> candidates{fs::path("/usr/lib/llvm-" + major) / "lib/clang" / major,
                                         fs::path("/usr/lib/clang") / major, fs::path("/usr/local/lib/clang") / major};
        if (lib_.path[0] != '\0') candidates.push_back(fs::path(lib_.path).parent_path() / "clang" / major);
        for (const auto& candidate : candidates) {
            std::error_code ignored;
            if (fs::exists(candidate / "include/stddef.h", ignored)) return candidate.string();
        }
        return {};
    }

    CXTranslationUnit parse(const std::string& file, CXUnsavedFile* unsaved, bool report = true) {
        std::vector<const char*> argv;
        for (const auto& argument : arguments_) argv.push_back(argument.c_str());
        CXTranslationUnit unit = nullptr;
        const auto error = lib_.clang_parseTranslationUnit2(index_, file.c_str(), argv.data(),
                                                            static_cast<int>(argv.size()), unsaved, unsaved ? 1 : 0,
                                                            NEURA_LIBCLANG_HEADER_PARSE_OPTIONS, &unit);
        if (error != CXError_Success || unit == nullptr)
            throw Diagnostic(site_, "cannot parse C header " + header_.string());
        if (report) {
            for (unsigned i = 0; i < lib_.clang_getNumDiagnostics(unit); ++i) {
                auto diagnostic = lib_.clang_getDiagnostic(unit, i);
                const auto severity = lib_.clang_getDiagnosticSeverity(diagnostic);
                if (severity >= CXDiagnostic_Error) {
                    const auto message =
                        text(lib_.clang_formatDiagnostic(diagnostic, lib_.clang_defaultDiagnosticDisplayOptions()));
                    lib_.clang_disposeDiagnostic(diagnostic);
                    lib_.clang_disposeTranslationUnit(unit);
                    throw Diagnostic(site_, "C import: " + message);
                }
                lib_.clang_disposeDiagnostic(diagnostic);
            }
        }
        return unit;
    }

    // --- Types ----------------------------------------------------------------

    static Mapped unsupported(std::string reason) {
        Mapped mapped;
        mapped.reason = std::move(reason);
        return mapped;
    }

    Mapped scalar(std::string carrier, std::string type, CXType ctype) {
        Mapped mapped;
        mapped.kind = Mapped::Kind::Scalar;
        mapped.scalar = {std::move(carrier), std::move(type), lib_.clang_Type_getSizeOf(ctype),
                         lib_.clang_Type_getAlignOf(ctype)};
        return mapped;
    }

    Mapped integer(CXType ctype, bool is_signed) {
        switch (lib_.clang_Type_getSizeOf(ctype)) {
        case 1: return is_signed ? scalar("Int8", "int8", ctype) : scalar("UInt8", "uint8", ctype);
        case 2: return is_signed ? scalar("Int16", "int16", ctype) : scalar("UInt16", "uint16", ctype);
        case 4: return is_signed ? scalar("Int32", "int32", ctype) : scalar("UInt32", "uint32", ctype);
        case 8: return is_signed ? scalar("Int64", "int64", ctype) : scalar("UInt64", "uint64", ctype);
        default: return unsupported("an integer type of " + std::to_string(lib_.clang_Type_getSizeOf(ctype)) +
                                    " bytes");
        }
    }

    Mapped map(CXType type) {
        const auto canonical = lib_.clang_getCanonicalType(type);
        switch (canonical.kind) {
        case CXType_Void: {
            Mapped mapped;
            mapped.kind = Mapped::Kind::Void;
            return mapped;
        }
        case CXType_Bool: return scalar("Bool", "bool", canonical);
        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
        case CXType_WChar: return integer(canonical, true);
        case CXType_Char_U:
        case CXType_UChar:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
        case CXType_Char16:
        case CXType_Char32: return integer(canonical, false);
        case CXType_Float: return scalar("Float32", "float32", canonical);
        case CXType_Double: return scalar("Float64", "float64", canonical);
        case CXType_Pointer:
        case CXType_BlockPointer: {
            Mapped mapped;
            mapped.kind = Mapped::Kind::Pointer;
            const auto pointee = lib_.clang_getPointeeType(canonical);
            const auto pointee_kind = lib_.clang_getCanonicalType(pointee).kind;
            mapped.c_string = (pointee_kind == CXType_Char_S || pointee_kind == CXType_Char_U) &&
                              lib_.clang_isConstQualifiedType(pointee);
            return mapped;
        }
        case CXType_Enum:
            return map(lib_.clang_getEnumDeclIntegerType(lib_.clang_getTypeDeclaration(canonical)));
        case CXType_Record: {
            const auto declaration = lib_.clang_getTypeDeclaration(canonical);
            if (lib_.clang_getCursorKind(declaration) == CXCursor_UnionDecl)
                return unsupported("a union passed by value");
            const auto& found = record(declaration);
            if (!found.supported) return unsupported("struct " + found.name + ": " + found.reason);
            Mapped mapped;
            mapped.kind = Mapped::Kind::Struct;
            mapped.record = found.name;
            return mapped;
        }
        case CXType_LongDouble: return unsupported("long double");
        case CXType_Complex: return unsupported("_Complex");
        case CXType_Int128:
        case CXType_UInt128: return unsupported("a 128-bit integer");
        default: return unsupported("the C type " + text(lib_.clang_getTypeSpelling(type)));
        }
    }

    std::string record_name(CXCursor declaration) {
        if (const auto named = typedef_names_.find(key(declaration)); named != typedef_names_.end())
            return named->second;
        return text(lib_.clang_getCursorSpelling(declaration));
    }

    const Record& record(CXCursor declaration) {
        const auto definition = lib_.clang_getCursorDefinition(declaration);
        const auto cursor = lib_.clang_Cursor_isNull(definition) ? declaration : definition;
        const auto id = key(cursor);
        if (const auto found = records_.find(id); found != records_.end()) return found->second;
        auto& result = records_[id];
        result.name = neura_name(record_name(cursor));
        if (lib_.clang_Cursor_isNull(definition)) {
            result.reason = "its definition is not visible";
            return result;
        }
        if (result.name.empty() || lib_.clang_Cursor_isAnonymous(cursor)) {
            result.reason = "an anonymous struct";
            return result;
        }
        const auto type = lib_.clang_getCursorType(cursor);
        result.size = lib_.clang_Type_getSizeOf(type);
        std::vector<Field> fields;
        std::string reason;
        long long position = 0;
        long long alignment = 1;
        visit(cursor, [&](CXCursor child) {
            if (!reason.empty() || lib_.clang_getCursorKind(child) != CXCursor_FieldDecl) return;
            Field field;
            field.name = neura_name(text(lib_.clang_getCursorSpelling(child)));
            if (lib_.clang_Cursor_isBitField(child)) {
                reason = "bit-field " + field.name;
                return;
            }
            const auto field_type = lib_.clang_getCanonicalType(lib_.clang_getCursorType(child));
            auto element_type = field_type;
            if (field_type.kind == CXType_ConstantArray) {
                field.count = lib_.clang_getArraySize(field_type);
                element_type = lib_.clang_getArrayElementType(field_type);
            } else if (field_type.kind == CXType_IncompleteArray) {
                reason = "flexible array member " + field.name;
                return;
            }
            field.type = map(element_type);
            if (field.type.kind == Mapped::Kind::Unsupported || field.type.kind == Mapped::Kind::Void) {
                reason = "field " + field.name + " has " + field.type.reason;
                return;
            }
            if (field.count >= 0 && field.type.kind == Mapped::Kind::Struct) {
                reason = "field " + field.name + " is an array of structs";
                return;
            }
            field.offset = lib_.clang_Cursor_getOffsetOfField(child) / 8;
            field.size = lib_.clang_Type_getSizeOf(element_type);
            const auto field_align = lib_.clang_Type_getAlignOf(element_type);
            position = (position + field_align - 1) / field_align * field_align;
            if (position != field.offset) {
                reason = "a packed or explicitly aligned layout";
                return;
            }
            position += field.size * (field.count >= 0 ? field.count : 1);
            alignment = std::max(alignment, field_align);
            fields.push_back(std::move(field));
        });
        auto& stored = records_[id];
        if (reason.empty() && fields.empty()) reason = "an empty struct";
        if (reason.empty() && (position + alignment - 1) / alignment * alignment != stored.size)
            reason = "a packed or explicitly aligned layout";
        stored.reason = reason;
        stored.supported = reason.empty();
        stored.fields = std::move(fields);
        if (stored.supported) emit_record(stored);
        return stored;
    }

    // --- Neura source ---------------------------------------------------------

    static std::string layout_of(const Mapped& type) {
        switch (type.kind) {
        case Mapped::Kind::Scalar:
            if (type.scalar.align == type.scalar.size) return "c_value_layout." + type.scalar.carrier;
            return "c_layout.value_sized(c_layout.Carrier." + type.scalar.carrier + ", " +
                   std::to_string(type.scalar.size) + ", " + std::to_string(type.scalar.align) + ")";
        case Mapped::Kind::Pointer: return "c_value_layout.Address";
        case Mapped::Kind::Struct: return type.record + ".layout()";
        default: return {};
        }
    }

    static std::string neura_type(const Mapped& type) {
        switch (type.kind) {
        case Mapped::Kind::Scalar: return type.scalar.type;
        case Mapped::Kind::Pointer: return "c_segment.MemorySegment";
        case Mapped::Kind::Struct: return type.record;
        default: return "void";
        }
    }

    static std::string default_value(const Mapped& type) {
        if (type.kind == Mapped::Kind::Scalar) {
            if (type.scalar.type == "bool") return "false";
            if (type.scalar.type == "float32" || type.scalar.type == "float64") return "0.0";
            return "0";
        }
        return "nil";
    }

    // The MemorySegment accessor suffix of a carrier: Int32 is int32, UInt8 is uint8.
    static std::string accessor(const std::string& carrier) {
        std::string name;
        for (const char c : carrier) name += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        return name;
    }

    void emit_record(const Record& record) {
        std::ostringstream out;
        out << "    /// @brief The C struct " << record.name << " (" << record.size << " bytes).\n";
        out << "    export let " << record.name << " = class (\n";
        for (const auto& field : record.fields) {
            if (field.count >= 0) {
                out << "        export let " << field.name << ": " << neura_type(field.type) << "[list] = [];\n";
            } else {
                out << "        export let " << field.name << ": " << neura_type(field.type) << " = "
                    << default_value(field.type) << ";\n";
            }
        }
        out << "\n        /// @brief The struct's memory layout.\n";
        out << "        export static let layout = func -> c_layout.MemoryLayout => c_layout.struct_layout(";
        for (std::size_t i = 0; i < record.fields.size(); ++i) {
            const auto& field = record.fields[i];
            out << (i > 0 ? ", " : "");
            if (field.count >= 0)
                out << "c_layout.sequence_layout(" << field.count << ", " << layout_of(field.type) << ")";
            else
                out << layout_of(field.type);
            out << ".with_name(\"" << field.name << "\")";
        }
        out << ");\n\n";
        out << "        /// @brief A value with the given fields.\n";
        out << "        export static let of = func(";
        bool first = true;
        for (const auto& field : record.fields) {
            out << (first ? "" : ", ") << field.name << "_value: " << neura_type(field.type)
                << (field.count >= 0 ? "[list]" : "");
            first = false;
        }
        out << ") -> " << record.name << " {\n";
        out << "            let value = new " << record.name << "();\n";
        for (const auto& field : record.fields)
            out << "            value." << field.name << " = " << field.name << "_value;\n";
        out << "            return value;\n        };\n\n";
        out << "        /// @brief The struct stored at the start of `segment`.\n";
        out << "        export static let read = func(segment: c_segment.MemorySegment) -> " << record.name << " {\n";
        out << "            let value = new " << record.name << "();\n";
        for (const auto& field : record.fields) {
            if (field.count >= 0) {
                out << "            for (let i: int64 = 0; i < " << field.count << "; i++) {\n";
                out << "                value." << field.name << ".append(" << read_at(field.type, std::to_string(field.offset) + " + i * " + std::to_string(field.size)) << ");\n";
                out << "            }\n";
            } else {
                out << "            value." << field.name << " = " << read_at(field.type, std::to_string(field.offset))
                    << ";\n";
            }
        }
        out << "            return value;\n        };\n\n";
        out << "        /// @brief Stores the struct at the start of `segment`; a nil field or a missing array element is zero.\n";
        out << "        export let write_to = func(segment: c_segment.MemorySegment) {\n";
        out << "            segment.as_slice(0, " << record.size << ").fill(0);\n";
        for (const auto& field : record.fields) {
            const auto offset = std::to_string(field.offset);
            if (field.count >= 0) {
                out << "            for (let i = 0; i < " << field.count << " && i < " << field.name << ".length; i++) {\n";
                out << "                " << write_at(field.type, offset + " + (i as int64) * " + std::to_string(field.size), field.name + "[i]") << "\n";
                out << "            }\n";
            } else {
                out << "            " << write_at(field.type, offset, field.name) << "\n";
            }
        }
        out << "        };\n    );\n\n";
        records_source_ += out.str();
        declared(record.name);
    }

    static std::string read_at(const Mapped& type, const std::string& offset) {
        switch (type.kind) {
        case Mapped::Kind::Scalar: return "segment.get_" + accessor(type.scalar.carrier) + "(" + offset + ")";
        case Mapped::Kind::Pointer: return "segment.get_address(" + offset + ")";
        case Mapped::Kind::Struct:
            return type.record + ".read(segment.as_slice(" + offset + ", " + type.record + ".layout().byte_size()))";
        default: return {};
        }
    }

    static std::string write_at(const Mapped& type, const std::string& offset, const std::string& value) {
        switch (type.kind) {
        case Mapped::Kind::Scalar:
            return "segment.set_" + accessor(type.scalar.carrier) + "(" + offset + ", " + value + ");";
        case Mapped::Kind::Pointer:
            return "if (" + value + " != nil) {\n                segment.set_address(" + offset + ", " + value +
                   ");\n            }";
        case Mapped::Kind::Struct:
            return "if (" + value + " != nil) {\n                " + value + ".write_to(segment.as_slice(" + offset +
                   ", " + type.record + ".layout().byte_size()));\n            }";
        default: return {};
        }
    }

    static std::string result_of(const Mapped& type, const std::string& value) {
        if (type.kind == Mapped::Kind::Pointer) return "c_value.as_segment(" + value + ")";
        const auto& t = type.scalar.type;
        if (t == "bool") return "c_value.as_bool(" + value + ")";
        if (t == "int32") return "c_value.as_int32(" + value + ")";
        if (t == "int64") return "c_value.as_int64(" + value + ")";
        if (t == "uint64") return "c_value.as_uint64(" + value + ")";
        if (t == "float32") return "c_value.as_float32(" + value + ")";
        if (t == "float64") return "c_value.as_float64(" + value + ")";
        return "c_value.as_int64(" + value + ") as " + t;
    }

    bool declared(const std::string& name) { return !names_.insert(name).second; }

    // --- Declarations ---------------------------------------------------------

    void declaration(CXCursor cursor) {
        if (!in_header(cursor)) return;
        switch (lib_.clang_getCursorKind(cursor)) {
        case CXCursor_FunctionDecl: function(cursor); break;
        case CXCursor_StructDecl:
            if (lib_.clang_isCursorDefinition(cursor)) {
                const auto& found = record(cursor);
                if (!found.supported && !found.name.empty()) unsupported_.emplace(found.name, found.reason);
            }
            break;
        case CXCursor_TypedefDecl: {
            const auto underlying = lib_.clang_getCanonicalType(lib_.clang_getTypedefDeclUnderlyingType(cursor));
            if (underlying.kind == CXType_Record &&
                lib_.clang_getCursorKind(lib_.clang_getTypeDeclaration(underlying)) == CXCursor_StructDecl) {
                const auto& found = record(lib_.clang_getTypeDeclaration(underlying));
                if (!found.supported && !found.name.empty()) unsupported_.emplace(found.name, found.reason);
            }
            break;
        }
        case CXCursor_EnumDecl: enumeration(cursor); break;
        case CXCursor_VarDecl: global(cursor); break;
        case CXCursor_MacroDefinition:
            if (!lib_.clang_Cursor_isMacroBuiltin(cursor)) {
                const auto name = text(lib_.clang_getCursorSpelling(cursor));
                if (lib_.clang_Cursor_isMacroFunctionLike(cursor)) {
                    unsupported_.emplace(neura_name(name), "a function-like macro");
                } else {
                    // A macro defined as nothing, such as an include guard,
                    // has only its name as a token.
                    CXToken* tokens = nullptr;
                    unsigned count = 0;
                    lib_.clang_tokenize(unit_, lib_.clang_getCursorExtent(cursor), &tokens, &count);
                    if (tokens != nullptr) lib_.clang_disposeTokens(unit_, tokens, count);
                    if (count > 1) macro_names_.push_back(name);
                }
            }
            break;
        default: break;
        }
    }

    void enumeration(CXCursor cursor) {
        const auto integer_type = map(lib_.clang_getEnumDeclIntegerType(cursor));
        visit(cursor, [&](CXCursor child) {
            if (lib_.clang_getCursorKind(child) != CXCursor_EnumConstantDecl) return;
            const auto name = neura_name(text(lib_.clang_getCursorSpelling(child)));
            if (declared(name)) return;
            const bool is_unsigned = integer_type.scalar.type.starts_with("u");
            const auto value = is_unsigned ? std::to_string(lib_.clang_getEnumConstantDeclUnsignedValue(child))
                                           : std::to_string(lib_.clang_getEnumConstantDeclValue(child));
            constants_ << "    /// @brief The C enumerator " << name << ".\n";
            constants_ << "    export const " << name << ": " << integer_type.scalar.type << " = " << value << ";\n";
        });
    }

    std::string symbol(const std::string& name) {
        symbols_.insert(name);
        return "neura_library().find_or_trap(\"" + name + "\")";
    }

    void global(CXCursor cursor) {
        const auto storage = lib_.clang_Cursor_getStorageClass(cursor);
        if (storage == CX_SC_Static) return;
        const auto c_name = text(lib_.clang_getCursorSpelling(cursor));
        const auto name = neura_name(c_name);
        const auto type = lib_.clang_getCursorType(cursor);
        const auto size = lib_.clang_Type_getSizeOf(type);
        if (declared(name + "_segment")) return;
        std::ostringstream out;
        out << "    /// @brief The memory of the C global " << c_name << ".\n";
        out << "    export let " << name << "_segment = func -> c_segment.MemorySegment => " << symbol(c_name)
            << ".reinterpret(" << std::max<long long>(size, 0) << ");\n";
        const auto mapped = map(type);
        if (mapped.kind == Mapped::Kind::Scalar || mapped.kind == Mapped::Kind::Pointer ||
            mapped.kind == Mapped::Kind::Struct) {
            const auto t = neura_type(mapped);
            out << "    /// @brief Reads the C global " << c_name << ".\n";
            out << "    export let get_" << name << " = func -> " << t << " {\n";
            out << "        let segment = " << name << "_segment();\n";
            out << "        return " << read_at(mapped, "0") << ";\n    };\n";
            if (!lib_.clang_isConstQualifiedType(type)) {
                out << "    /// @brief Writes the C global " << c_name << ".\n";
                out << "    export let set_" << name << " = func(value: " << t << ") {\n";
                out << "        let segment = " << name << "_segment();\n";
                out << "        " << write_at(mapped, "0", "value") << "\n    };\n";
            }
        }
        functions_ << out.str() << "\n";
    }

    void function(CXCursor cursor) {
        const auto c_name = text(lib_.clang_getCursorSpelling(cursor));
        const auto name = neura_name(c_name);
        if (functions_seen_.contains(c_name)) return;
        functions_seen_.insert(c_name);
        const auto type = lib_.clang_getCursorType(cursor);
        const bool variadic = lib_.clang_isFunctionTypeVariadic(type) != 0;
        const auto result = map(lib_.clang_getResultType(type));
        if (result.kind == Mapped::Kind::Unsupported) {
            unsupported_.emplace(name, "its result is " + result.reason);
            return;
        }
        std::vector<std::pair<std::string, Mapped>> parameters;
        const auto count = lib_.clang_Cursor_getNumArguments(cursor);
        for (int i = 0; i < count; ++i) {
            const auto argument = lib_.clang_Cursor_getArgument(cursor, static_cast<unsigned>(i));
            auto parameter = neura_name(text(lib_.clang_getCursorSpelling(argument)));
            if (parameter.empty()) parameter = "arg" + std::to_string(i);
            auto mapped = map(lib_.clang_getCursorType(argument));
            if (mapped.kind == Mapped::Kind::Unsupported || mapped.kind == Mapped::Kind::Void) {
                unsupported_.emplace(name, "parameter " + parameter + " is " + mapped.reason);
                return;
            }
            if (variadic && mapped.kind == Mapped::Kind::Struct) {
                unsupported_.emplace(name, "a variadic function with a struct parameter");
                return;
            }
            parameters.emplace_back(parameter, std::move(mapped));
        }
        if (declared(name)) {
            unsupported_.emplace(name, "the name is already declared");
            return;
        }
        // An inline function has no external definition: call an exported
        // wrapper compiled into the image's native library.
        std::string target = c_name;
        if (lib_.clang_Cursor_isFunctionInlined(cursor)) {
            if (variadic) {
                unsupported_.emplace(name, "a variadic inline function");
                return;
            }
            target = "neura_inline_" + c_name;
            shim_ << text(lib_.clang_getTypeSpelling(lib_.clang_getResultType(type))) << " " << target << "(";
            for (int i = 0; i < count; ++i) {
                const auto argument = lib_.clang_Cursor_getArgument(cursor, static_cast<unsigned>(i));
                shim_ << (i > 0 ? ", " : "") << text(lib_.clang_getTypeSpelling(lib_.clang_getCursorType(argument)))
                      << " a" << i;
            }
            shim_ << (count == 0 ? "void" : "") << ") {\n    "
                  << (result.kind == Mapped::Kind::Void ? "" : "return ") << c_name << "(";
            for (int i = 0; i < count; ++i) shim_ << (i > 0 ? ", " : "") << "a" << i;
            shim_ << ");\n}\n";
        }
        std::string descriptor = result.kind == Mapped::Kind::Void ? "c_descriptor.of_void(" : "c_descriptor.of(" + layout_of(result);
        for (std::size_t i = 0; i < parameters.size(); ++i)
            descriptor += (i == 0 && result.kind == Mapped::Kind::Void ? "" : ", ") + layout_of(parameters[i].second);
        descriptor += ")";

        std::ostringstream out;
        const auto result_type = neura_type(result);
        if (variadic) {
            out << "    /// @brief Calls the variadic C function " << c_name
                << ". Each further argument is an int when it fits, a long long otherwise, a double, an int for a "
                   "bool, or an address.\n";
            out << "    export let " << name << " = func(";
            for (const auto& [parameter, mapped] : parameters) out << parameter << ": " << neura_type(mapped) << ", ";
            out << "neura_rest: ...) -> " << result_type << " {\n";
            out << "        let neura_arguments: c_variadic.Argument[list] = [];\n";
            for (const auto& [parameter, mapped] : parameters) out << "        neura_arguments.append(" << pack(mapped, parameter) << ");\n";
            out << "        for (let neura_argument in neura_rest) {\n            neura_arguments.append(neura_argument);\n        }\n";
            const auto call = "c_method.MethodHandle.call_variadic(" + symbol(target) + ", " + descriptor + ", neura_arguments)";
            if (result.kind == Mapped::Kind::Void) out << "        " << call << ";\n";
            else out << "        return " << result_of(result, call) << ";\n";
            out << "    };\n\n";
            functions_ << out.str();
            return;
        }
        out << "    let neura_handle_" << name << ": c_method.MethodHandle = nil;\n";
        out << "    let neura_call_" << name << " = func -> c_method.MethodHandle {\n";
        out << "        if (neura_handle_" << name << " == nil) {\n";
        out << "            neura_handle_" << name << " = c_linker.native_linker().downcall_handle(" << symbol(target)
            << ",\n                " << descriptor << ");\n";
        out << "        }\n        return neura_handle_" << name << ";\n    };\n";
        const bool has_string = std::ranges::any_of(parameters, [](const auto& p) { return p.second.c_string; });
        out << "    /// @brief Calls the C function " << c_name << ".";
        if (has_string) out << " A `string` overload passes NUL-terminated copies of `const char*` arguments.";
        out << "\n";
        for (int variant = 0; variant < (has_string ? 2 : 1); ++variant) {
            out << (variant == 0 ? "    export let " + name + " = func(" : "    or func(");
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                const auto& [parameter, mapped] = parameters[i];
                out << (i > 0 ? ", " : "") << parameter << ": "
                    << (variant == 1 && mapped.c_string ? "string" : neura_type(mapped));
            }
            out << ") -> " << result_type << " {\n";
            body(out, name, parameters, result);
            out << "    }" << (variant == 0 && has_string ? "\n" : ";\n\n");
        }
        functions_ << out.str();
    }

    static std::string pack(const Mapped& type, const std::string& value) {
        if (type.kind == Mapped::Kind::Pointer) return "c_variadic.Argument.Object(" + value + ")";
        const auto& t = type.scalar.type;
        if (t == "bool") return "c_variadic.Argument.Boolean(" + value + ")";
        if (t == "float32") return "c_variadic.Argument.Floating32(" + value + ")";
        if (t == "float64") return "c_variadic.Argument.Floating(" + value + ")";
        if (t.starts_with("u")) return "c_variadic.Argument.Unsigned(" + value + " as uint64)";
        return "c_variadic.Argument.Signed(" + value + " as int64)";
    }

    static void body(std::ostringstream& out, const std::string& name,
                     const std::vector<std::pair<std::string, Mapped>>& parameters, const Mapped& result) {
        const bool structs = result.kind == Mapped::Kind::Struct ||
                             std::ranges::any_of(parameters, [](const auto& p) { return p.second.kind == Mapped::Kind::Struct; });
        std::string arguments;
        if (structs) out << "        let neura_arena = c_arena.of_confined();\n";
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            const auto& [parameter, mapped] = parameters[i];
            arguments += (i > 0 ? ", " : "");
            if (mapped.kind == Mapped::Kind::Struct) {
                out << "        let neura_" << parameter << " = neura_arena.allocate(" << mapped.record << ".layout());\n";
                out << "        if (" << parameter << " != nil) {\n            " << parameter << ".write_to(neura_" << parameter << ");\n        }\n";
                arguments += "neura_" + parameter;
            } else {
                arguments += parameter;
            }
        }
        const auto call = "neura_call_" + name + "()." + (structs ? "invoke_with(neura_arena" + std::string(parameters.empty() ? "" : ", ") : "invoke(") + arguments + ")";
        if (result.kind == Mapped::Kind::Void) {
            out << "        " << call << ";\n";
            if (structs) out << "        neura_arena.close();\n";
        } else if (result.kind == Mapped::Kind::Struct) {
            out << "        let neura_result = " << result.record << ".read(c_value.as_segment(" << call << "));\n";
            out << "        neura_arena.close();\n        return neura_result;\n";
        } else if (structs) {
            out << "        let neura_result = " << result_of(result, call) << ";\n";
            out << "        neura_arena.close();\n        return neura_result;\n";
        } else {
            out << "        return " << result_of(result, call) << ";\n";
        }
    }

    // --- Macros ---------------------------------------------------------------

    void macros() {
        if (macro_names_.empty()) return;
        // Evaluate each object-like macro as the initializer of a variable in
        // a translation unit that includes the header.
        std::string source = "#include \"" + fs::absolute(header_).generic_string() + "\"\n";
        for (const auto& name : macro_names_)
            source += "static const __auto_type neura_macro_" + name + " = (" + name + ");\n";
        const std::string file = (header_.parent_path() / "neura-macros.c").string();
        CXUnsavedFile unsaved{file.c_str(), source.c_str(), static_cast<unsigned long>(source.size())};
        auto unit = parse(file, &unsaved, false);
        std::map<std::string, CXCursor> variables;
        visit(lib_.clang_getTranslationUnitCursor(unit), [&](CXCursor cursor) {
            if (lib_.clang_getCursorKind(cursor) != CXCursor_VarDecl) return;
            const auto name = text(lib_.clang_getCursorSpelling(cursor));
            if (name.starts_with("neura_macro_")) variables.emplace(name.substr(12), cursor);
        });
        for (const auto& c_name : macro_names_) {
            const auto name = neura_name(c_name);
            const auto found = variables.find(c_name);
            if (found == variables.end()) {
                unsupported_.emplace(name, "a macro that is not a constant expression");
                continue;
            }
            const auto type = lib_.clang_getCanonicalType(lib_.clang_getCursorType(found->second));
            auto result = lib_.clang_Cursor_Evaluate(found->second);
            std::optional<std::string> declaration;
            switch (result == nullptr ? CXEval_UnExposed : lib_.clang_EvalResult_getKind(result)) {
            case CXEval_Int: {
                const auto mapped = map(type);
                if (mapped.kind != Mapped::Kind::Scalar || mapped.scalar.type == "bool") break;
                const bool is_unsigned = lib_.clang_EvalResult_isUnsignedInt(result) != 0;
                const auto value = is_unsigned ? std::to_string(lib_.clang_EvalResult_getAsUnsigned(result))
                                               : std::to_string(lib_.clang_EvalResult_getAsLongLong(result));
                declaration = mapped.scalar.type + " = " + value;
                break;
            }
            case CXEval_Float: {
                const bool single = type.kind == CXType_Float;
                const double value = lib_.clang_EvalResult_getAsDouble(result);
                declaration = float_constant(value, single);
                break;
            }
            case CXEval_StrLiteral: {
                const char* chars = lib_.clang_EvalResult_getAsStr(result);
                if (chars != nullptr) {
                    if (auto literal = string_literal(chars)) declaration = "string = " + *literal;
                }
                break;
            }
            default: break;
            }
            if (!declaration) {
                // A string macro initializes a char pointer, which does not
                // evaluate; read its string literal instead.
                std::optional<std::string> literal;
                std::function<void(CXCursor)> find = [&](CXCursor node) {
                    if (literal) return;
                    if (lib_.clang_getCursorKind(node) == CXCursor_StringLiteral) {
                        literal = decode_c_string(text(lib_.clang_getCursorSpelling(node)));
                        return;
                    }
                    visit(node, find);
                };
                find(found->second);
                if (literal) {
                    if (auto neura_literal = string_literal(*literal)) declaration = "string = " + *neura_literal;
                }
            }
            if (result != nullptr) lib_.clang_EvalResult_dispose(result);
            if (!declaration) {
                unsupported_.emplace(name, "a macro that is not an integer, floating, or string constant");
                continue;
            }
            if (declared(name)) continue;
            constants_ << "    /// @brief The C macro " << c_name << ".\n";
            constants_ << "    export const " << name << ": " << *declaration << ";\n";
        }
        lib_.clang_disposeTranslationUnit(unit);
    }

    // The bytes of a simple C string literal token, or nothing for a prefix,
    // an octal or hex escape, or concatenated literals.
    static std::optional<std::string> decode_c_string(const std::string& token) {
        if (token.size() < 2 || token.front() != '"' || token.back() != '"') return std::nullopt;
        std::string result;
        for (std::size_t i = 1; i + 1 < token.size(); ++i) {
            char c = token[i];
            if (c == '"') return std::nullopt;
            if (c == '\\') {
                if (i + 2 >= token.size()) return std::nullopt;
                switch (token[++i]) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '\'': c = '\''; break;
                default: return std::nullopt;
                }
            }
            result += c;
        }
        return result;
    }

    static std::string float_constant(double value, bool single) {
        std::array<char, 64> buffer{};
        const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        std::string text(buffer.data(), error == std::errc{} ? end : buffer.data());
        const bool plain = !text.empty() && text.find_first_of("eEni") == std::string::npos;
        if (plain && text.find('.') == std::string::npos) text += ".0";
        if (single) {
            if (plain) return "float32 = " + text;
            std::uint32_t bits = 0;
            const auto narrowed = static_cast<float>(value);
            std::memcpy(&bits, &narrowed, sizeof bits);
            return "float32 = c_float32.from_bits(" + std::to_string(bits) + ")";
        }
        if (plain) return "float64 = " + text;
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        return "float64 = c_float64.from_bits(" + std::to_string(bits) + ")";
    }

    // A Neura string literal for UTF-8 text, or nothing when it holds a
    // control character a literal cannot spell.
    static std::optional<std::string> string_literal(std::string_view text) {
        std::string literal = "\"";
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            if (c == '"') literal += "\\\"";
            else if (c == '\\') literal += "\\\\";
            else if (c == '\n') literal += "\\n";
            else if (c == '\t') literal += "\\t";
            else if (c == '\r') literal += "\\r";
            else if (byte < 0x20 || byte == 0x7f) return std::nullopt;
            else literal += c;
        }
        return literal + "\"";
    }

    // --- Assembly ------------------------------------------------------------

    CImportResult finish() {
        CImportResult result;
        result.module_name = module_name_;
        if (!shim_.str().empty())
            result.shim = "#include \"" + fs::absolute(header_).generic_string() + "\"\n" + shim_.str();
        result.unsupported = unsupported_;
        result.symbols.assign(symbols_.begin(), symbols_.end());
        const bool embedded = linked_ || !result.shim.empty();
        std::ostringstream out;
        out << "// Generated by neura from the C header " << header_.filename().generic_string() << ". Do not edit.\n";
        for (const auto& [name, reason] : unsupported_) out << "// Not imported: " << name << ": " << reason << "\n";
        out << "let c_arena = import:Std.DynamicForeign.Arena;\n"
               "let c_descriptor = import:Std.DynamicForeign.FunctionDescriptor;\n"
               "let c_float32 = import:Std.Core.Float32;\n"
               "let c_float64 = import:Std.Core.Float64;\n"
               "let c_layout = import:Std.DynamicForeign.MemoryLayout;\n"
               "let c_linker = import:Std.DynamicForeign.Linker;\n"
               "let c_lookup = import:Std.DynamicForeign.SymbolLookup;\n"
               "let c_method = import:Std.DynamicForeign.MethodHandle;\n"
               "let c_segment = import:Std.DynamicForeign.MemorySegment;\n"
               "let c_value = import:Std.DynamicForeign.ForeignValue;\n"
               "let c_value_layout = import:Std.DynamicForeign.ValueLayout;\n"
               "let c_variadic = import:Std.Core.Variadic;\n\n";
        out << "/// @brief The declarations of the C header " << header_.filename().generic_string()
            << " (ForeignInterop.md, C imports). Calls need --allow-native.\n";
        out << "export let " << module_name_ << " = module (\n";
        out << "    let neura_lookup: c_lookup.SymbolLookup = nil;\n";
        out << "    let neura_library = func -> c_lookup.SymbolLookup {\n";
        out << "        if (neura_lookup == nil) {\n";
        out << "            neura_lookup = "
            << (embedded ? "c_lookup.embedded().or_else(c_lookup.default_lookup())" : "c_lookup.default_lookup()")
            << ";\n        }\n        return neura_lookup;\n    };\n\n";
        out << constants_.str() << (constants_.str().empty() ? "" : "\n");
        out << records_source_;
        out << functions_.str();
        out << ");\n";
        result.source = out.str();
        return result;
    }

    NeuraLibclang& lib_;
    fs::path header_;
    bool linked_;
    std::string module_name_;
    const CImportOptions& options_;
    Span site_;
    std::vector<std::string> arguments_;
    CXIndex index_ = nullptr;
    CXTranslationUnit unit_ = nullptr;
    std::map<std::string, std::string> typedef_names_;
    std::map<std::string, Record> records_;
    std::set<std::string> names_;
    std::set<std::string> functions_seen_;
    std::set<std::string> symbols_;
    std::vector<std::string> macro_names_;
    std::map<std::string, std::string> unsupported_;
    std::ostringstream constants_;
    std::ostringstream functions_;
    std::ostringstream shim_;
    std::string records_source_;
};

} // namespace

CImportResult import_c_header(const std::filesystem::path& header, bool linked, const std::string& module_name,
                              const CImportOptions& options, const Span& site) {
    auto& lib = libclang(options, site);
    return Importer(lib, header, linked, module_name, options, site).run();
}

} // namespace neura
