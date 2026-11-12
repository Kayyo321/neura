#include "analysis.hpp"

#include "cli/source_loader.hpp"
#include "neura-ir/attributes.hpp"
#include "neura-ir/tags.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <sstream>
#include <tuple>

namespace neura::tooling {
namespace fs = std::filesystem;
namespace {
constexpr std::size_t none = static_cast<std::size_t>(-1);
bool contains(const Span &span, std::size_t offset) {
    return offset >= span.offset && offset <= span.offset + span.length;
}
unsigned modifier(std::string_view name) {
    const auto found = std::find(semantic_modifiers.begin(), semantic_modifiers.end(), name);
    return found == semantic_modifiers.end() ? 0u : 1u << static_cast<unsigned>(found - semantic_modifiers.begin());
}
std::string read_text(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>{input}, {}};
}
std::string type_text(const IrType &type) {
    if (type.kind == TypeKind::Array || type.kind == TypeKind::List)
        return (type.arguments.empty() ? "?" : type_text(type.arguments.front())) +
               (type.kind == TypeKind::Array ? "[]" : "[list]");
    std::string name = type.name;
    if (name.empty())
        switch (type.kind) {
        case TypeKind::Void:
            name = "void";
            break;
        case TypeKind::Bool:
            name = "bool";
            break;
        case TypeKind::String:
            name = "string";
            break;
        case TypeKind::Callable:
            name = "func";
            break;
        case TypeKind::Tuple:
            name = "tuple";
            break;
        default:
            name = "?";
        }
    if (!type.arguments.empty()) {
        name += '<';
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            if (i)
                name += ", ";
            name += type_text(type.arguments[i]);
        }
        name += '>';
    }
    return name;
}
std::string syntax_type(const TypeSyntax &type) {
    std::string name = type.reference ? "&" : "";
    name += type.name;
    if (!type.arguments.empty()) {
        name += '<';
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            if (i)
                name += ", ";
            name += syntax_type(type.arguments[i]);
        }
        name += '>';
    }
    for (auto kind : type.collections)
        name += kind == CollectionKind::Array ? "[]" : "[list]";
    return name;
}
std::string signature(const Declaration &d) {
    std::string text = d.exported ? "export " : "";
    if (d.static_member)
        text += "static ";
    if (d.kind == Declaration::Kind::Function) {
        text += d.name + "(";
        for (std::size_t i = 0; i < d.parameters.size(); ++i) {
            if (i)
                text += ", ";
            text += d.parameters[i].name + ": " + syntax_type(d.parameters[i].type);
            if (d.parameters[i].variadic)
                text += "...";
        }
        text += ")";
        if (d.type)
            text += " -> " + syntax_type(*d.type);
        else
            text += " -> void";
    } else {
        switch (d.kind) {
        case Declaration::Kind::Class:
            text += "class ";
            break;
        case Declaration::Kind::Module:
            text += "module ";
            break;
        case Declaration::Kind::Interface:
            text += "interface ";
            break;
        case Declaration::Kind::Enum:
            text += "enum ";
            break;
        default:
            text += d.last ? "last " : d.constant ? "const " : "let ";
        }
        text += d.name;
        if (d.type)
            text += ": " + syntax_type(*d.type);
    }
    if (!d.accessors.empty()) {
        text += " { ";
        for (const auto &a : d.accessors)
            text += a.setter ? "set; " : "get; ";
        text += '}';
    }
    return text;
}
Span named_span(const EditorDocument &doc, const Span &span, std::string_view name, bool last = false) {
    Span result = span;
    result.length = name.size();
    for (const auto &token : doc.tokens) {
        if (token.span.offset < span.offset)
            continue;
        if (token.span.offset > span.offset + span.length)
            break;
        if (token.text == name) {
            result = token.span;
            if (!last)
                break;
        }
    }
    return result;
}
int symbol_kind(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Module:
        return 2;
    case SymbolKind::Class:
        return 5;
    case SymbolKind::Interface:
        return 11;
    case SymbolKind::Enum:
        return 10;
    case SymbolKind::Variant:
        return 22;
    case SymbolKind::Field:
        return 8;
    case SymbolKind::Function:
        return 12;
    case SymbolKind::TypeParameter:
        return 26;
    case SymbolKind::Alias:
        return 5;
    default:
        return 13;
    }
}
int symbol_token(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Module:
        return 27;
    case SymbolKind::Class:
        return 2;
    case SymbolKind::Interface:
        return 4;
    case SymbolKind::Enum:
        return 3;
    case SymbolKind::Variant:
        return 9;
    case SymbolKind::Field:
        return 8;
    case SymbolKind::Function:
        return 11;
    case SymbolKind::Parameter:
        return 6;
    case SymbolKind::TypeParameter:
        return 5;
    case SymbolKind::Alias:
        return 1;
    default:
        return 7;
    }
}
} // namespace

std::string path_uri(std::string_view path) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string uri = "file://";
    if (path.empty() || path.front() != '/')
        uri += '/';
    for (unsigned char c : path) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':')
            uri += static_cast<char>(c);
        else {
            uri += '%';
            uri += digits[c >> 4];
            uri += digits[c & 15];
        }
    }
    return uri;
}
std::string uri_path(std::string_view uri) {
    if (!uri.starts_with("file://"))
        return {};
    uri.remove_prefix(7);
    if (!uri.empty() && uri.front() != '/') {
        if (uri.starts_with("localhost/"))
            uri.remove_prefix(9);
        else
            return {};
    }
    std::string path;
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            int a = hex(uri[i + 1]), b = hex(uri[i + 2]);
            if (a < 0 || b < 0)
                return {};
            char c = static_cast<char>(a * 16 + b);
            if (!c)
                return {};
            path += c;
            i += 2;
        } else
            path += uri[i];
    }
#ifdef _WIN32
    if (path.size() > 2 && path[0] == '/' && path[2] == ':')
        path.erase(0, 1);
#endif
    return fs::weakly_canonical(fs::absolute(path)).generic_string();
}
Json source_position(std::string_view source, std::size_t offset) {
    offset = std::min(offset, source.size());
    std::size_t line = 0, column = 0;
    for (std::size_t i = 0; i < offset;) {
        const unsigned char c = static_cast<unsigned char>(source[i]);
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < offset && source[i + 1] == '\n')
                ++i;
            ++line;
            column = 0;
            ++i;
            continue;
        }
        std::size_t bytes = (c & 0x80) == 0      ? 1
                            : (c & 0xe0) == 0xc0 ? 2
                            : (c & 0xf0) == 0xe0 ? 3
                            : (c & 0xf8) == 0xf0 ? 4
                                                 : 1;
        column += bytes == 4 ? 2 : 1;
        i += std::min(bytes, offset - i);
    }
    return Json{{"line", line}, {"character", column}};
}
std::size_t source_offset(std::string_view source, const Json &position) {
    const auto wanted_line = std::max<std::int64_t>(0, position["line"].integer()),
               wanted_column = std::max<std::int64_t>(0, position["character"].integer());
    std::int64_t line = 0, column = 0;
    std::size_t i = 0;
    for (; i < source.size();) {
        unsigned char c = static_cast<unsigned char>(source[i]);
        if (line == wanted_line && column >= wanted_column)
            return i;
        if (c == '\r' || c == '\n') {
            if (line == wanted_line)
                return i;
            if (c == '\r' && i + 1 < source.size() && source[i + 1] == '\n')
                ++i;
            ++line;
            column = 0;
            ++i;
            continue;
        }
        std::size_t bytes = (c & 0x80) == 0      ? 1
                            : (c & 0xe0) == 0xc0 ? 2
                            : (c & 0xf0) == 0xe0 ? 3
                            : (c & 0xf8) == 0xf0 ? 4
                                                 : 1;
        column += bytes == 4 ? 2 : 1;
        i += std::min(bytes, source.size() - i);
    }
    return i;
}
Json source_range(std::string_view source, const Span &span) {
    return Json{{"start", source_position(source, span.offset)},
                {"end", source_position(source, span.offset + span.length)}};
}

void AnalysisSession::parse_document(EditorDocument &document) {
    document.syntax_diagnostics.clear();
    document.tokens = lex_recovering(document.source, document.path, document.syntax_diagnostics);
    document.syntax = parse_recovering(document.tokens, document.syntax_diagnostics);
}
EditorDocument &AnalysisSession::open(std::string uri, std::string source, std::int64_t version) {
    const auto path = uri_path(uri);
    if (path.empty())
        throw std::runtime_error("Neura language services require a file URI");
    auto &document = documents[path];
    document.uri = std::move(uri);
    document.path = path;
    document.source = std::move(source);
    document.version = version;
    document.open = true;
    parse_document(document);
    invalidate();
    return document;
}
EditorDocument *AnalysisSession::document(std::string_view uri) {
    const auto path = uri_path(uri);
    if (path.empty())
        return nullptr;
    auto found = documents.find(path);
    if (found != documents.end())
        return &found->second;
    if (!fs::is_regular_file(path))
        return nullptr;
    EditorDocument doc;
    doc.uri = path_uri(path);
    doc.path = path;
    doc.source = read_text(path);
    parse_document(doc);
    return &documents.emplace(path, std::move(doc)).first->second;
}
void AnalysisSession::close(std::string_view uri) {
    const auto path = uri_path(uri);
    documents.erase(path);
    invalidate();
}
void AnalysisSession::change(const Json &params) {
    auto *doc = document(params["textDocument"]["uri"].string());
    if (!doc)
        return;
    const auto version = params["textDocument"]["version"].integer(doc->version + 1);
    if (version <= doc->version)
        return;
    for (const auto &change : params["contentChanges"].items()) {
        if (change["range"].is_object()) {
            auto begin = source_offset(doc->source, change["range"]["start"]),
                 end = source_offset(doc->source, change["range"]["end"]);
            if (end < begin)
                throw std::runtime_error("invalid change range");
            doc->source.replace(begin, end - begin, change["text"].string());
        } else
            doc->source = change["text"].string();
    }
    doc->version = version;
    parse_document(*doc);
    invalidate();
}
void AnalysisSession::index_workspace() {
    if (workspace_indexed_)
        return;
    workspace_indexed_ = true;
    std::error_code error;
    std::size_t count = 0;
    const auto options = fs::directory_options::skip_permission_denied;
    for (fs::recursive_directory_iterator it(root, options, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (it->is_directory(error) && (name == ".git" || name == "node_modules" || name == "build" || name == "dist" ||
                                        name == ".neura" || name == ".neura-vscode" || name == "third_party")) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(error) && it->path().extension() == ".nra") {
            if (++count > 5000)
                break;
            try {
                document(path_uri(fs::weakly_canonical(it->path()).generic_string()));
            } catch (const std::exception &) {
            }
        }
    }
}
void AnalysisSession::analyze(std::string_view uri) {
    auto *active = document(uri);
    if (!active)
        return;
    if (analyzed_ == active->path)
        return;
    analyzed_ = active->path;
    diagnostics.clear();
    symbols.clear();
    references.clear();
    program.reset();
    sources.clear();
    const SourceMapScope source_scope(sources);
    std::map<std::string, std::string, std::less<>> overlays;
    for (const auto &[path, doc] : documents)
        if (doc.open)
            overlays[path] = doc.source;
    const SourceOverlayScope overlay_scope(overlays, trusted);
    DiagnosticSink warnings;
    const DiagnosticSinkScope warning_scope(warnings);
    try {
        program = lower_sources({active->path}, source_roots, library);
    } catch (const Diagnostic &error) {
        diagnostics.push_back(error);
        for (const auto &next : error.following())
            diagnostics.push_back(next);
    } catch (const std::exception &error) {
        diagnostics.emplace_back(Span{active->path}, error.what());
    }
    diagnostics.insert(diagnostics.end(), warnings.entries().begin(), warnings.entries().end());
    for (const auto &file : sources.files()) {
        auto found = documents.find(file->name());
        if (found == documents.end()) {
            EditorDocument doc;
            doc.path = file->name();
            doc.uri = path_uri(doc.path);
            doc.source = file->text();
            doc.library = file->kind() != SourceKind::User;
            parse_document(doc);
            documents.emplace(doc.path, std::move(doc));
        } else
            found->second.library = file->kind() != SourceKind::User;
    }
    for (auto &[path, doc] : documents)
        index_syntax(doc);
    if (program)
        index_compiler();
    // Lexically resolved references remain useful in an incomplete function.
    // Successful compiler references always take precedence at the same token.
    std::set<std::pair<std::string, std::size_t>> occupied;
    for (const auto &ref : references)
        occupied.emplace(ref.span.file, ref.span.offset);
    for (const auto &symbol : symbols)
        occupied.emplace(symbol.file, symbol.selection.offset);
    for (const auto &[path, doc] : documents)
        for (const auto &token : doc.tokens) {
            if (token.kind != TokenKind::Identifier || occupied.contains({path, token.span.offset}))
                continue;
            const auto previous = token.span.offset
                                      ? doc.source.substr(0, token.span.offset).find_last_not_of(" \t\r\n")
                                      : std::string::npos;
            if (previous != std::string::npos && doc.source[previous] == '.')
                continue;
            if (auto id = resolve_name(doc, token.text, token.span.offset))
                references.push_back({token.span, *id, false, -1});
        }
}

void AnalysisSession::index_syntax(EditorDocument &doc) {
    const Span file_scope{doc.path, 1, 1, 0, doc.source.size()};
    std::function<void(const Declaration &, std::size_t, const Span &, bool)> declaration;
    std::function<void(const Statement &, std::size_t, const Span &)> statement;
    std::function<void(const Expr &, std::size_t, const Span &)> expression;
    const auto add = [&](std::string name, const Span &selected, const Span &body, const Span &scope,
                         std::size_t parent, int kind, int type, unsigned mods, std::string detail = {},
                         std::string type_name = {}, const Declaration *decl = nullptr) {
        EditorSymbol symbol;
        symbol.name = std::move(name);
        symbol.file = doc.path;
        symbol.selection = selected;
        symbol.span = body;
        symbol.scope = scope;
        symbol.parent = parent;
        symbol.kind = kind;
        symbol.token_type = type;
        symbol.modifiers = mods | (doc.library ? modifier("defaultLibrary") : 0);
        symbol.detail = std::move(detail);
        symbol.type_name = std::move(type_name);
        symbol.declaration = decl;
        symbol.qualified = parent == none ? symbol.name : symbols[parent].qualified + "." + symbol.name;
        if (decl && decl->documentation)
            symbol.documentation = decl->documentation->text;
        symbols.push_back(std::move(symbol));
        return symbols.size() - 1;
    };
    expression = [&](const Expr &e, std::size_t parent, const Span &scope) {
        if (e.lambda)
            declaration(*e.lambda, parent, scope, false);
        if (e.match)
            statement(*e.match, parent, scope);
        for (const auto &operand : e.operands)
            expression(operand, parent, scope);
    };
    statement = [&](const Statement &s, std::size_t parent, const Span &scope) {
        const Span next_scope = s.kind == Statement::Kind::Block ? s.span : scope;
        if (s.declaration)
            declaration(*s.declaration, parent, next_scope, false);
        for (const auto &token : s.bindings)
            add(token.text, token.span, token.span, next_scope, parent, 13, 7,
                s.constant_bindings ? modifier("readonly") : 0);
        for (const auto &e : s.expressions)
            expression(e, parent, next_scope);
        for (const auto *body : {&s.body, &s.alternative, &s.initializer, &s.step})
            for (const auto &child : *body)
                statement(child, parent, next_scope);
        for (const auto &arm : s.arms) {
            for (const auto &token : arm.bindings)
                add(token.text, token.span, token.span, arm.span, parent, 13, 7, modifier("readonly"));
            for (const auto &child : arm.body)
                statement(child, parent, arm.span);
            if (arm.value)
                expression(*arm.value, parent, arm.span);
        }
        for (const auto &arm : s.select_arms) {
            if (arm.binding)
                add(arm.binding->text, arm.binding->span, arm.binding->span, arm.span, parent, 13, 7,
                    arm.constant_binding ? modifier("readonly") : 0);
            for (const auto &child : arm.body)
                statement(child, parent, arm.span);
            if (arm.operation)
                expression(*arm.operation, parent, arm.span);
        }
    };
    declaration = [&](const Declaration &d, std::size_t parent, const Span &scope, bool variant) {
        int kind = 13, type = 7;
        switch (d.kind) {
        case Declaration::Kind::Class:
            kind = 5;
            type = 2;
            break;
        case Declaration::Kind::Module:
            kind = 2;
            type = 27;
            break;
        case Declaration::Kind::Enum:
            kind = 10;
            type = 3;
            break;
        case Declaration::Kind::Interface:
            kind = 11;
            type = 4;
            break;
        case Declaration::Kind::Function:
            kind = parent != none && symbols[parent].kind == 5 ? 6 : 12;
            type = kind == 6 ? 12 : 11;
            break;
        default:
            if (parent != none && (symbols[parent].kind == 5 || symbols[parent].kind == 11)) {
                kind = d.accessors.empty() ? 8 : 7;
                type = d.accessors.empty() ? 26 : 8;
            }
        }
        if (variant) {
            kind = 22;
            type = 9;
        }
        if (d.name == "init" && kind == 6) {
            kind = 9;
            type = 28;
        } else if (d.name == "destroy" && kind == 6)
            type = 29;
        unsigned mods = modifier("declaration") | (d.has_body ? modifier("definition") : 0) |
                        (d.constant ? modifier("readonly") : 0) | (d.exported ? modifier("exported") : 0) |
                        (d.static_member ? modifier("static") : 0) | (d.override_member ? modifier("override") : 0) |
                        (d.last && !d.initializer ? modifier("deferred") : 0);
        for (const auto &tag : d.tags) {
            if (tag.name == "Deprecated")
                mods |= modifier("deprecated");
            if (tag.name == "Synchronized")
                mods |= modifier("synchronized");
            if (tag.name == "Immutable")
                mods |= modifier("readonly");
        }
        auto selected = d.name_span.value_or(named_span(doc, d.span, d.name));
        const auto id = add(d.name, selected, d.span, scope, parent, kind, type, mods, signature(d),
                            d.type ? syntax_type(*d.type) : "", &d);
        for (const auto &generic : d.generics)
            add(generic.name, named_span(doc, generic.span, generic.name), generic.span, d.span, id, 26, 5,
                modifier("declaration"));
        for (const auto &parameter : d.parameters)
            add(parameter.name, named_span(doc, parameter.span, parameter.name), parameter.span, d.span, id, 13, 6,
                modifier("declaration"), parameter.name + ": " + syntax_type(parameter.type),
                syntax_type(parameter.type));
        for (const auto &member : d.members)
            declaration(member, id, d.span, d.kind == Declaration::Kind::Enum);
        for (const auto &overload : d.overloads)
            declaration(overload, parent, scope, false);
        for (const auto &s : d.body)
            statement(s, id, d.span);
        for (const auto &accessor : d.accessors) {
            if (accessor.setter)
                add("value", accessor.keyword, accessor.span, accessor.span, id, 13, 6, 0,
                    "value: " + (d.type ? syntax_type(*d.type) : "?"), d.type ? syntax_type(*d.type) : "");
            for (const auto &s : accessor.body)
                statement(s, id, accessor.span);
            if (accessor.expression)
                expression(*accessor.expression, id, accessor.span);
        }
        if (d.initializer)
            expression(*d.initializer, parent, scope);
    };
    for (const auto &s : doc.syntax.statements)
        statement(s, none, file_scope);
}

std::optional<std::size_t> AnalysisSession::resolve_name(const EditorDocument &doc, std::string_view name,
                                                         std::size_t offset) const {
    std::optional<std::size_t> result;
    std::size_t length = std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const auto &s = symbols[i];
        if (s.file != doc.path || s.name != name || !contains(s.scope, offset))
            continue;
        if (s.kind == 13 && s.token_type != 6 && s.selection.offset > offset)
            continue;
        if (s.scope.length < length ||
            (s.scope.length == length && (!result || s.selection.offset > symbols[*result].selection.offset))) {
            result = i;
            length = s.scope.length;
        }
    }
    return result;
}

void AnalysisSession::index_compiler() {
    if (!program)
        return;
    std::map<SymbolId, std::size_t> ids;
    for (SymbolId id = 0; id < program->symbols.size(); ++id) {
        const auto &original = program->symbols[id];
        if (original.name.empty() || original.name.starts_with('$') || original.kind == SymbolKind::File)
            continue;
        std::optional<std::size_t> best;
        std::size_t distance = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            const auto &s = symbols[i];
            if (s.file != original.span.file || s.name != original.name)
                continue;
            const auto delta = s.selection.offset > original.span.offset ? s.selection.offset - original.span.offset
                                                                         : original.span.offset - s.selection.offset;
            if (delta < distance) {
                best = i;
                distance = delta;
            }
        }
        if (!best) {
            auto doc = documents.find(original.span.file);
            if (doc == documents.end())
                continue;
            EditorSymbol symbol;
            symbol.name = original.name;
            symbol.qualified = original.name;
            symbol.file = original.span.file;
            symbol.span = original.span;
            symbol.selection = named_span(doc->second, original.span, original.name);
            symbol.scope = Span{original.span.file, 1, 1, 0, doc->second.source.size()};
            symbol.kind = symbol_kind(original.kind);
            symbol.token_type = symbol_token(original.kind);
            symbols.push_back(std::move(symbol));
            best = symbols.size() - 1;
        }
        ids[id] = *best;
        auto &s = symbols[*best];
        s.compiler_id = id;
        s.type_name = type_text(original.type);
        if (original.constant)
            s.modifiers |= modifier("readonly");
        if (original.exported)
            s.modifiers |= modifier("exported");
        if (deferred_binding(original))
            s.modifiers |= modifier("deferred");
        if (has_tag(original, TagId::Deprecated))
            s.modifiers |= modifier("deprecated");
        if (original.kind == SymbolKind::Function) {
            std::string detail = s.name + "(";
            for (std::size_t p = 0; p < original.parameters.size(); ++p) {
                if (p)
                    detail += ", ";
                const auto &param = program->symbols[original.parameters[p]];
                detail += param.name + ": " + type_text(param.type);
            }
            s.detail = detail + ") -> " + type_text(original.type);
        } else if (s.detail.empty() || original.kind == SymbolKind::Local || original.kind == SymbolKind::Parameter)
            s.detail = s.name + ": " + type_text(original.type);
    }
    // Type references disappear during lowering unless the editor records
    // them alongside the resolved declaration types. Pair syntax with those
    // compiler types instead of guessing qualified names by spelling.
    std::function<void(const TypeSyntax &, const IrType &)> type_references;
    type_references = [&](const TypeSyntax &syntax, const IrType &original) {
        const IrType *type = &original;
        for (std::size_t count = 0; count < syntax.collections.size(); ++count)
            if ((type->kind == TypeKind::Array || type->kind == TypeKind::List) && !type->arguments.empty())
                type = &type->arguments.front();
        const auto found = ids.find(type->symbol);
        const auto doc = documents.find(syntax.span.file);
        if (found != ids.end() && doc != documents.end()) {
            auto name = syntax.name;
            if (const auto separator = name.find_last_of('.'); separator != std::string::npos)
                name.erase(0, separator + 1);
            const auto span = named_span(doc->second, syntax.span, name, true);
            if (span.length == name.size() && doc->second.source.substr(span.offset, span.length) == name)
                references.push_back({span, found->second, false, symbols[found->second].token_type});
        }
        for (std::size_t i = 0; i < syntax.arguments.size() && i < type->arguments.size(); ++i)
            type_references(syntax.arguments[i], type->arguments[i]);
    };
    for (const auto &item : symbols) {
        if (item.compiler_id == no_symbol)
            continue;
        const auto &resolved = program->symbols[item.compiler_id];
        if (item.declaration) {
            const auto &declaration = *item.declaration;
            if (declaration.type)
                type_references(*declaration.type, resolved.type);
            if (declaration.base && resolved.base != no_symbol)
                type_references(*declaration.base, program->symbols[resolved.base].type);
            for (std::size_t i = 0; i < declaration.interfaces.size() && i < resolved.interfaces.size(); ++i)
                type_references(declaration.interfaces[i], resolved.interfaces[i]);
            if (declaration.initializer && declaration.initializer->kind == Expr::Kind::Import &&
                resolved.type.symbol != no_symbol) {
                const auto target = ids.find(resolved.type.symbol);
                const auto doc = documents.find(item.file);
                if (target != ids.end() && doc != documents.end()) {
                    const auto &imported = program->symbols[resolved.type.symbol];
                    const auto selected = named_span(doc->second, declaration.initializer->span, imported.name, true);
                    if (doc->second.source.substr(selected.offset, selected.length) == imported.name)
                        references.push_back({selected, target->second, false, 27});
                }
            }
        } else if (item.token_type == 6 && item.parent != none && symbols[item.parent].declaration) {
            for (const auto &parameter : symbols[item.parent].declaration->parameters)
                if (parameter.name == item.name)
                    type_references(parameter.type, resolved.type);
        } else if (item.token_type == 5 && item.parent != none && symbols[item.parent].declaration) {
            for (const auto &generic : symbols[item.parent].declaration->generics)
                if (generic.name == item.name) {
                    if (resolved.bound && !generic.bounds.empty())
                        type_references(generic.bounds.front(), *resolved.bound);
                    for (std::size_t i = 1; i < generic.bounds.size() && i - 1 < resolved.additional_bounds.size(); ++i)
                        type_references(generic.bounds[i], resolved.additional_bounds[i - 1]);
                }
        }
    }
    const auto reference = [&](const IrNode &node, bool write) {
        SymbolId symbol = node.symbol;
        int type = -1;
        if (symbol == no_symbol || symbol >= program->symbols.size())
            return;
        if (const auto *a = find_attribute(program->symbols[symbol], AttributeId::AccessorOf);
            a && a->value.items.size() >= 2) {
            symbol = a->value.items[0].symbol;
            type = a->value.items[1].integer ? 25 : 24;
        }
        const auto found = ids.find(symbol);
        if (found == ids.end())
            return;
        const auto &s = symbols[found->second];
        auto doc = documents.find(node.span.file);
        if (doc == documents.end())
            return;
        bool hit = false;
        Span selection;
        for (const auto &token : doc->second.tokens) {
            if (token.span.offset < node.span.offset)
                continue;
            if (token.span.offset + token.span.length > node.span.offset + node.span.length)
                break;
            if (token.text == s.name) {
                selection = token.span;
                hit = true;
                if (program->symbols[symbol].kind == SymbolKind::Function) {
                    const auto after =
                        doc->second.source.find_first_not_of(" \t\r\n", token.span.offset + token.span.length);
                    if (after != std::string::npos &&
                        (doc->second.source[after] == '(' || doc->second.source[after] == '<'))
                        break;
                }
            }
        }
        if (!hit)
            return;
        if (selection.file == s.file && selection.offset == s.selection.offset)
            return;
        if (type < 0 && program->symbols[symbol].kind == SymbolKind::Field)
            type = find_attribute(program->symbols[symbol], AttributeId::AccessorGet) ||
                           find_attribute(program->symbols[symbol], AttributeId::AccessorSet)
                       ? 26
                       : s.token_type;
        references.push_back({selection, found->second, write, type});
    };
    std::function<void(const IrNode &, bool)> visit = [&](const IrNode &node, bool write) {
        reference(node, write);
        for (std::size_t i = 0; i < node.operands.size(); ++i)
            visit(node.operands[i], (node.op == IrOp::Store && i == 0) || write);
        for (const auto &child : node.body)
            visit(child, false);
        for (const auto &child : node.alternative)
            visit(child, false);
    };
    for (const auto &body : program->bodies)
        for (const auto &node : body.instructions)
            visit(node, false);
    std::sort(references.begin(), references.end(), [](const auto &a, const auto &b) {
        return std::tie(a.span.file, a.span.offset, a.symbol) < std::tie(b.span.file, b.span.offset, b.symbol);
    });
    references.erase(std::unique(references.begin(), references.end(),
                                 [](const auto &a, const auto &b) {
                                     return a.span.file == b.span.file && a.span.offset == b.span.offset &&
                                            a.symbol == b.symbol && a.token_type == b.token_type;
                                 }),
                     references.end());
}

std::optional<std::size_t> AnalysisSession::symbol_index_at(const EditorDocument &doc, std::size_t offset) const {
    for (std::size_t i = 0; i < symbols.size(); ++i)
        if (symbols[i].file == doc.path && contains(symbols[i].selection, offset))
            return i;
    for (const auto &r : references)
        if (r.span.file == doc.path && contains(r.span, offset))
            return r.symbol;
    return std::nullopt;
}
const EditorSymbol *AnalysisSession::symbol_at(const EditorDocument &doc, std::size_t offset) const {
    const auto id = symbol_index_at(doc, offset);
    return id ? &symbols[*id] : nullptr;
}
Json AnalysisSession::location(const Span &span) const {
    const auto found = documents.find(span.file);
    if (found == documents.end())
        return Json{};
    return Json{{"uri", found->second.uri}, {"range", source_range(found->second.source, span)}};
}
Json AnalysisSession::diagnostics_for(const EditorDocument &doc) const {
    Json::Array result;
    for (const auto &d : diagnostics) {
        if (d.span.file != doc.path)
            continue;
        Json::Array related;
        for (const auto &label : d.labels()) {
            if (label.primary)
                continue;
            auto loc = location(label.span);
            if (!loc.is_null())
                related.emplace_back(
                    Json{{"location", loc}, {"message", label.text.empty() ? "Related location" : label.text}});
        }
        std::string message = d.what();
        if (const auto *data = d.data()) {
            for (const auto &note : data->notes)
                message += "\n\n" + note;
            for (const auto &help : data->helps)
                message += "\n\nHelp: " + help;
        }
        Json item{{"range", source_range(doc.source, d.span)},
                  {"severity", d.severity() == Severity::Warning ? 2 : 1},
                  {"source", "neura"},
                  {"message", message},
                  {"relatedInformation", related}};
        if (!d.code().empty()) {
            item["code"] = std::string(d.code());
            item["codeDescription"] =
                Json{{"href", "https://kayyo321.github.io/neura/docs/diagnostics/" + std::string(d.code()) + ".html"}};
        }
        if (d.code() == "W0001")
            item["tags"] = Json::Array{2};
        result.push_back(std::move(item));
    }
    return result;
}

Json AnalysisSession::semantic_tokens(const EditorDocument &doc) const {
    struct Mark {
        Span span;
        int type;
        unsigned mods;
        int priority;
    };
    std::map<std::size_t, Mark> marks;
    const auto put = [&](Span span, int type, unsigned mods, int priority) {
        if (span.length == 0 || span.offset >= doc.source.size())
            return;
        auto old = marks.find(span.offset);
        if (old == marks.end() || old->second.priority <= priority)
            marks.insert_or_assign(span.offset, Mark{std::move(span), type, mods, priority});
    };
    for (const auto &t : doc.tokens) {
        int type = -1;
        switch (t.kind) {
        case TokenKind::Keyword:
            type = 14;
            break;
        case TokenKind::Integer:
        case TokenKind::Float:
            type = 18;
            break;
        case TokenKind::String:
            type = 17;
            break;
        case TokenKind::Symbol:
            if (t.text != "(" && t.text != ")" && t.text != "{" && t.text != "}" && t.text != "[" && t.text != "]" &&
                t.text != ";" && t.text != ",")
                type = 20;
            break;
        default:
            break;
        }
        if (type >= 0)
            put(t.span, type, 0, 0);
    }
    for (const auto &s : symbols)
        if (s.file == doc.path) {
            if (s.name == "value" && s.declaration == nullptr && s.selection.length != s.name.size())
                continue;
            put(s.selection, s.token_type, s.modifiers, 3);
        }
    for (const auto &r : references)
        if (r.span.file == doc.path)
            put(r.span, r.token_type >= 0 ? r.token_type : symbols[r.symbol].token_type,
                (symbols[r.symbol].modifiers & ~(modifier("declaration") | modifier("definition"))) |
                    (r.write ? modifier("modification") : 0),
                4);
    std::function<void(const Declaration &)> declaration;
    std::function<void(const Statement &)> statement;
    std::function<void(const Expr &)> expression;
    std::function<void(const TypeSyntax &)> type;
    type = [&](const TypeSyntax &t) {
        for (const auto &token : doc.tokens)
            if (token.span.offset >= t.span.offset && token.span.offset < t.span.offset + t.span.length &&
                token.kind == TokenKind::Identifier) {
                int kind = 1;
                if (auto id = resolve_name(doc, token.text, token.span.offset)) {
                    const auto &target = symbols[*id];
                    if (target.kind == 5 || target.kind == 10 || target.kind == 11 || target.kind == 26)
                        kind = target.token_type;
                }
                put(token.span, kind, 0, 1);
            }
        for (const auto &a : t.arguments)
            type(a);
        if (t.anonymous)
            declaration(*t.anonymous);
    };
    expression = [&](const Expr &e) {
        if (e.type)
            type(*e.type);
        for (const auto &t : e.type_arguments)
            type(t);
        if (e.kind == Expr::Kind::Spawn) {
            auto span = e.span;
            span.length = 5;
            put(span, 14, 0, 2);
        }
        if (e.lambda)
            declaration(*e.lambda);
        for (const auto &operand : e.operands)
            expression(operand);
        if (e.match)
            statement(*e.match);
    };
    statement = [&](const Statement &s) {
        if (s.declaration)
            declaration(*s.declaration);
        if (s.kind == Statement::Kind::Scope || s.kind == Statement::Kind::Select)
            put(s.keyword, 14, 0, 2);
        if (s.binding_type)
            type(*s.binding_type);
        for (const auto &e : s.expressions)
            expression(e);
        for (const auto *body : {&s.body, &s.alternative, &s.initializer, &s.step})
            for (const auto &child : *body)
                statement(child);
        for (const auto &arm : s.arms) {
            for (const auto &child : arm.body)
                statement(child);
            if (arm.value)
                expression(*arm.value);
        }
        for (const auto &arm : s.select_arms) {
            for (const auto &child : arm.body)
                statement(child);
            if (arm.operation)
                expression(*arm.operation);
        }
    };
    declaration = [&](const Declaration &d) {
        if (d.type)
            type(*d.type);
        if (d.base)
            type(*d.base);
        for (const auto &t : d.interfaces)
            type(t);
        for (const auto &g : d.generics)
            for (const auto &bound : g.bounds)
                type(bound);
        for (const auto &p : d.parameters)
            type(p.type);
        for (const auto &tag : d.tags) {
            put(tag.name_span, 21, 0, 3);
            for (const auto &argument : tag.arguments)
                if (argument.name_span)
                    put(*argument.name_span, 6, 0, 2);
        }
        for (const auto &a : d.accessors) {
            put(a.keyword, a.setter ? 23 : 22, modifier("declaration"), 5);
            for (const auto &s : a.body)
                statement(s);
            if (a.expression)
                expression(*a.expression);
        }
        for (const auto &child : d.members)
            declaration(child);
        for (const auto &child : d.overloads)
            declaration(child);
        for (const auto &s : d.body)
            statement(s);
        if (d.initializer)
            expression(*d.initializer);
    };
    for (const auto &s : doc.syntax.statements)
        statement(s);
    // Comments are trivia, so walk gaps between compiler tokens. String
    // contents cannot be mistaken for comments by this scan.
    std::size_t cursor = 0;
    for (const auto &token : doc.tokens) {
        const auto end = std::min(token.span.offset, doc.source.size());
        while (cursor < end) {
            if (cursor + 1 < end && doc.source[cursor] == '/' && doc.source[cursor + 1] == '/') {
                const auto begin = cursor;
                while (cursor < end && doc.source[cursor] != '\n' && doc.source[cursor] != '\r')
                    ++cursor;
                const bool documentation = begin + 2 < doc.source.size() && doc.source[begin + 2] == '/';
                Span span{doc.path, 1, 1, begin, cursor - begin};
                put(span, 16, documentation ? modifier("documentation") : 0, 0);
            } else
                ++cursor;
        }
        cursor = std::max(cursor, token.span.offset + token.span.length);
    }
    Json::Array data;
    std::int64_t previous_line = 0, previous_start = 0;
    std::size_t consumed = 0;
    for (const auto &[offset, mark] : marks) {
        if (offset < consumed)
            continue;
        const auto start = source_position(doc.source, offset),
                   end = source_position(doc.source, offset + mark.span.length);
        if (start["line"].integer() != end["line"].integer())
            continue;
        const auto line = start["line"].integer(), character = start["character"].integer(),
                   length = end["character"].integer() - character;
        if (length <= 0)
            continue;
        data.emplace_back(line - previous_line);
        data.emplace_back(line == previous_line ? character - previous_start : character);
        data.emplace_back(length);
        data.emplace_back(mark.type);
        data.emplace_back(mark.mods);
        previous_line = line;
        previous_start = character;
        consumed = offset + mark.span.length;
    }
    return Json{{"data", data}};
}

Json AnalysisSession::completion(const EditorDocument &doc, std::size_t offset) const {
    Json::Array items;
    std::set<std::string> seen;
    std::size_t start = offset;
    while (start && ((doc.source[start - 1] >= 'A' && doc.source[start - 1] <= 'Z') ||
                     (doc.source[start - 1] >= 'a' && doc.source[start - 1] <= 'z') ||
                     (doc.source[start - 1] >= '0' && doc.source[start - 1] <= '9') || doc.source[start - 1] == '_'))
        --start;
    const auto prefix = doc.source.substr(start, offset - start);
    const auto before = start ? doc.source.find_last_not_of(" \t\r\n", start - 1) : std::string::npos;
    if (before != std::string::npos && doc.source[before] == '#') {
        for (const auto &tag : tag_registry()) {
            if (tag.status == TagStatus::Reserved || !std::string_view(tag.name).starts_with(prefix))
                continue;
            items.emplace_back(Json{{"label", tag.name},
                                    {"kind", 14},
                                    {"detail", tag_signature(tag) + " — " + std::string(tag_status_name(tag.status))},
                                    {"documentation", Json{{"kind", "markdown"}, {"value", std::string(tag.meaning)}}},
                                    {"insertText", tag.name}});
        }
        return Json{{"isIncomplete", false}, {"items", items}};
    }
    std::optional<SymbolId> owner;
    bool member = false;
    if (before != std::string::npos && doc.source[before] == '.') {
        member = true;
        std::size_t end = before, begin = end;
        while (begin && std::isspace(static_cast<unsigned char>(doc.source[begin - 1])))
            --begin;
        end = begin;
        while (begin &&
               (std::isalnum(static_cast<unsigned char>(doc.source[begin - 1])) || doc.source[begin - 1] == '_'))
            --begin;
        const auto *receiver = symbol_at(doc, begin);
        if (receiver && program && receiver->compiler_id != no_symbol) {
            const auto &target = program->symbols[receiver->compiler_id];
            if (target.kind == SymbolKind::Module || target.kind == SymbolKind::Class ||
                target.kind == SymbolKind::Interface || target.kind == SymbolKind::Enum)
                owner = receiver->compiler_id;
            else if (target.type.symbol != no_symbol)
                owner = target.type.symbol;
            else if (target.base != no_symbol)
                owner = target.base;
        }
        if (!owner && receiver && receiver->declaration && receiver->declaration->initializer &&
            receiver->declaration->initializer->kind == Expr::Kind::Import) {
            auto name = receiver->declaration->initializer->text;
            auto last = name.find_last_of('.');
            name = name.substr(last == std::string::npos ? 0 : last + 1);
            for (const auto &s : symbols)
                if (s.name == name && s.kind == 2 && s.compiler_id != no_symbol) {
                    owner = s.compiler_id;
                    break;
                }
        }
    }
    for (const auto &symbol : symbols) {
        if (symbol.name.empty() || symbol.name.starts_with('$') || !symbol.name.starts_with(prefix))
            continue;
        if (member) {
            if (!owner || !program || symbol.compiler_id == no_symbol)
                continue;
            const auto &s = program->symbols[symbol.compiler_id];
            bool owned = s.owner == *owner;
            if (!owned && s.owner != no_symbol && is_static_scope(program->symbols[s.owner]))
                owned = program->symbols[s.owner].owner == *owner;
            if (!owned)
                continue;
            if (symbol.file != doc.path && !s.exported)
                continue;
        } else if (symbol.file != doc.path || !contains(symbol.scope, offset) ||
                   (symbol.kind == 13 && symbol.token_type != 6 && symbol.selection.offset > offset))
            continue;
        if (!seen.insert(symbol.name + symbol.detail).second)
            continue;
        int kind = symbol.kind == 12 || symbol.kind == 6  ? 3
                   : symbol.kind == 5                     ? 7
                   : symbol.kind == 11                    ? 8
                   : symbol.kind == 2                     ? 9
                   : symbol.kind == 10                    ? 13
                   : symbol.kind == 22                    ? 20
                   : symbol.token_type == 6               ? 6
                   : symbol.kind == 7 || symbol.kind == 8 ? 10
                                                          : 6;
        Json item{{"label", symbol.name},
                  {"kind", kind},
                  {"detail", symbol.detail},
                  {"documentation", Json{{"kind", "markdown"}, {"value", symbol.documentation}}},
                  {"textEdit", Json{{"range", source_range(doc.source, Span{doc.path, 1, 1, start, offset - start})},
                                    {"newText", symbol.name}}}};
        if (symbol.modifiers & modifier("deprecated"))
            item["tags"] = Json::Array{1};
        items.push_back(std::move(item));
    }
    if (!member)
        for (const auto &word : {"let",    "last", "const", "func", "class",  "interface", "module", "enum",
                                 "if",     "else", "while", "for",  "return", "match",     "scope",  "spawn",
                                 "select", "new",  "nil",   "true", "false",  "export",    "import"})
            if (std::string_view(word).starts_with(prefix))
                items.emplace_back(Json{{"label", word}, {"kind", 14}});
    return Json{{"isIncomplete", false}, {"items", items}};
}

Json AnalysisSession::document_symbols(const EditorDocument &doc) const {
    std::map<std::size_t, std::vector<std::size_t>> children;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const auto &s = symbols[i];
        if (s.file == doc.path && !(s.name == "value" && s.selection.length != 5))
            children[s.parent].push_back(i);
    }
    std::function<Json(std::size_t)> item = [&](std::size_t index) {
        const auto &s = symbols[index];
        Json::Array nested;
        for (auto child : children[index])
            nested.push_back(item(child));
        Json result{{"name", s.name},
                    {"detail", s.detail},
                    {"kind", s.kind},
                    {"range", source_range(doc.source, s.span)},
                    {"selectionRange", source_range(doc.source, s.selection)}};
        if (!nested.empty())
            result["children"] = nested;
        if (s.modifiers & modifier("deprecated"))
            result["tags"] = Json::Array{1};
        return result;
    };
    Json::Array result;
    for (auto index : children[none])
        result.push_back(item(index));
    return result;
}
Json AnalysisSession::entries() const {
    Json::Array result;
    if (!program)
        return result;
    std::set<SymbolId> requirements;
    for (SymbolId i = 0; i < program->symbols.size(); ++i) {
        const auto &s = program->symbols[i];
        if (s.kind != SymbolKind::Function || s.name != "main" || s.owner == no_symbol)
            continue;
        const auto &owner = program->symbols[s.owner];
        if (owner.kind != SymbolKind::Module || owner.name != "Entry" || owner.owner == no_symbol)
            continue;
        const auto &parent = program->symbols[owner.owner];
        if (parent.name == "Program" && parent.owner != no_symbol &&
            program->symbols[parent.owner].library_path == "std/program.nra")
            requirements.insert(i);
    }
    for (const auto &symbol : symbols) {
        if (symbol.compiler_id == no_symbol)
            continue;
        const auto &s = program->symbols[symbol.compiler_id];
        if (s.kind != SymbolKind::Function || !s.has_body)
            continue;
        bool entry = false;
        std::set<SymbolId> visited;
        for (auto base = s.overrides; base != no_symbol && visited.insert(base).second;
             base = program->symbols[base].overrides)
            if (requirements.contains(base)) {
                entry = true;
                break;
            }
        if (entry)
            result.emplace_back(Json{{"name", symbol.qualified},
                                     {"uri", path_uri(symbol.file)},
                                     {"range", location(symbol.selection)["range"]},
                                     {"kind", "application"}});
    }
    return result;
}
Json AnalysisSession::code_lenses(const EditorDocument &doc) const {
    Json::Array result;
    const auto discovered = entries();
    for (const auto &entry : discovered.items())
        if (entry["uri"].string() == doc.uri)
            for (const auto &[title, command] :
                 {std::pair{"Run", "neura.run"}, {"Build", "neura.build"}, {"Debug", "neura.debug"}})
                result.emplace_back(Json{
                    {"range", entry["range"]},
                    {"command", Json{{"title", title}, {"command", command}, {"arguments", Json::Array{doc.uri}}}}});
    bool test = false;
    for (const auto &s : symbols) {
        const auto *d = s.declaration;
        if (s.file != doc.path || !d || d->kind != Declaration::Kind::Function || !d->exported ||
            !d->name.starts_with("test_") || !d->parameters.empty() || !d->generics.empty() || !d->has_body ||
            (d->type && d->type->name != "void"))
            continue;
        if (s.parent == none || symbols[s.parent].kind != 2 || !symbols[s.parent].declaration ||
            !symbols[s.parent].declaration->generics.empty())
            continue;
        test = true;
    }
    if (test)
        result.emplace_back(
            Json{{"range",
                  Json{{"start", Json{{"line", 0}, {"character", 0}}}, {"end", Json{{"line", 0}, {"character", 0}}}}},
                 {"command",
                  Json{{"title", "Run Tests"}, {"command", "neura.testFile"}, {"arguments", Json::Array{doc.uri}}}}});
    return result;
}
Json AnalysisSession::inlay_hints(const EditorDocument &doc, const Json &range, InlayOptions options) const {
    Json::Array result;
    const auto begin = source_offset(doc.source, range["start"]), end = source_offset(doc.source, range["end"]);
    for (const auto &s : symbols) {
        if (!options.types || s.file != doc.path || s.selection.offset < begin || s.selection.offset > end ||
            !s.declaration || s.declaration->kind != Declaration::Kind::Binding || s.declaration->type ||
            s.compiler_id == no_symbol || s.type_name.empty() || s.type_name == "?" || s.type_name == "void")
            continue;
        const auto &type = program->symbols[s.compiler_id].type;
        if (type.kind == TypeKind::Callable || type.kind == TypeKind::Module)
            continue;
        result.emplace_back(Json{{"position", source_position(doc.source, s.selection.offset + s.selection.length)},
                                 {"label", ": " + s.type_name},
                                 {"kind", 1},
                                 {"paddingLeft", false},
                                 {"tooltip", "Inferred by the Neura compiler"}});
    }
    if (options.accessors) {
        std::map<std::size_t, unsigned> roles;
        for (const auto &reference : references)
            if (reference.span.file == doc.path && reference.span.offset >= begin && reference.span.offset <= end &&
                (reference.token_type == 24 || reference.token_type == 25))
                roles[reference.span.offset + reference.span.length] |= reference.token_type == 24 ? 1u : 2u;
        for (const auto &[offset, role] : roles)
            result.emplace_back(
                Json{{"position", source_position(doc.source, offset)},
                     {"label", role == 3   ? " (get, set)"
                               : role == 1 ? " (get)"
                                           : " (set)"},
                     {"kind", 1},
                     {"tooltip",
                      "Compiler-resolved accessor invocation; direct backing storage access has no accessor hint."}});
    }
    if (options.parameter_names && program) {
        std::vector<const Expr *> calls;
        std::function<void(const Expr &)> expression;
        std::function<void(const Statement &)> statement;
        std::function<void(const Declaration &)> declaration;
        expression = [&](const Expr &item) {
            if (item.kind == Expr::Kind::Call)
                calls.push_back(&item);
            for (const auto &child : item.operands)
                expression(child);
            if (item.lambda)
                declaration(*item.lambda);
            if (item.match)
                statement(*item.match);
        };
        statement = [&](const Statement &item) {
            if (item.declaration)
                declaration(*item.declaration);
            for (const auto &child : item.expressions)
                expression(child);
            for (const auto *body : {&item.body, &item.alternative, &item.initializer, &item.step})
                for (const auto &child : *body)
                    statement(child);
            for (const auto &arm : item.arms) {
                for (const auto &child : arm.body)
                    statement(child);
                if (arm.value)
                    expression(*arm.value);
            }
            for (const auto &arm : item.select_arms) {
                for (const auto &child : arm.body)
                    statement(child);
                if (arm.operation)
                    expression(*arm.operation);
            }
        };
        declaration = [&](const Declaration &item) {
            for (const auto &child : item.members)
                declaration(child);
            for (const auto &child : item.overloads)
                declaration(child);
            for (const auto &child : item.body)
                statement(child);
            for (const auto &accessor : item.accessors) {
                for (const auto &child : accessor.body)
                    statement(child);
                if (accessor.expression)
                    expression(*accessor.expression);
            }
            if (item.initializer)
                expression(*item.initializer);
        };
        for (const auto &item : doc.syntax.statements)
            statement(item);
        for (const auto *call : calls) {
            if (call->operands.empty() || !call->open)
                continue;
            const EditorSymbol *callee = nullptr;
            std::size_t best = 0;
            for (const auto &reference : references) {
                if (reference.span.file != doc.path || reference.span.offset < call->span.offset ||
                    reference.span.offset >= call->open->offset)
                    continue;
                const auto &candidate = symbols[reference.symbol];
                if (candidate.compiler_id != no_symbol &&
                    program->symbols[candidate.compiler_id].kind == SymbolKind::Function &&
                    (!callee || reference.span.offset >= best)) {
                    callee = &candidate;
                    best = reference.span.offset;
                }
            }
            if (!callee)
                continue;
            const auto &function = program->symbols[callee->compiler_id];
            const auto count = call->operands.size() - 1;
            const auto skip = function.parameters.size() > count ? function.parameters.size() - count : 0;
            for (std::size_t i = 0; i < count && i + skip < function.parameters.size(); ++i) {
                const auto &argument = call->operands[i + 1];
                const auto &parameter = program->symbols[function.parameters[i + skip]];
                if (argument.span.offset < begin || argument.span.offset > end || parameter.name.empty() ||
                    parameter.name.starts_with('$') ||
                    (argument.kind == Expr::Kind::Name && argument.text == parameter.name))
                    continue;
                result.emplace_back(Json{{"position", source_position(doc.source, argument.span.offset)},
                                         {"label", parameter.name + ":"},
                                         {"kind", 2},
                                         {"paddingRight", true},
                                         {"tooltip", callee->detail}});
            }
        }
    }
    if (options.captures && program) {
        std::function<void(const IrNode &)> visit = [&](const IrNode &node) {
            if (node.op == IrOp::MakeClosure && node.span.file == doc.path && node.span.offset >= begin &&
                node.span.offset <= end && !node.operands.empty()) {
                std::set<std::string> names;
                std::function<void(const IrNode &)> capture = [&](const IrNode &item) {
                    if (item.symbol < program->symbols.size()) {
                        const auto &name = program->symbols[item.symbol].name;
                        if (!name.empty() && !name.starts_with('$'))
                            names.insert(name);
                    }
                    for (const auto &child : item.operands)
                        capture(child);
                };
                for (const auto &input : node.operands)
                    capture(input);
                if (!names.empty()) {
                    std::string label = " captures: ";
                    for (const auto &name : names) {
                        if (label != " captures: ")
                            label += ", ";
                        label += name;
                    }
                    result.emplace_back(Json{{"position", source_position(doc.source, node.span.offset)},
                                             {"label", label + " "},
                                             {"kind", 1},
                                             {"tooltip", "Values captured by this closure, resolved by the compiler"}});
                }
            }
            for (const auto *body : {&node.operands, &node.body, &node.alternative})
                for (const auto &child : *body)
                    visit(child);
        };
        for (const auto &body : program->bodies)
            for (const auto &node : body.instructions)
                visit(node);
    }
    return result;
}
Json AnalysisSession::signature_help(const EditorDocument &doc, std::size_t offset) const {
    int depth = 0, argument = 0;
    std::optional<std::size_t> open;
    for (std::size_t i = doc.tokens.size(); i > 0;) {
        --i;
        const auto &t = doc.tokens[i];
        if (t.span.offset >= offset)
            continue;
        if (t.text == ")" || t.text == "]" || t.text == "}")
            ++depth;
        else if (t.text == "(" || t.text == "[" || t.text == "{") {
            if (depth)
                --depth;
            else if (t.text == "(") {
                open = i;
                break;
            } else
                return Json{};
        } else if (t.text == "," && !depth)
            ++argument;
    }
    if (!open || !*open)
        return Json{};
    std::size_t callee = *open - 1;
    if (doc.tokens[callee].text == ">") {
        int generic = 1;
        while (callee && generic) {
            --callee;
            if (doc.tokens[callee].text == ">")
                ++generic;
            else if (doc.tokens[callee].text == "<")
                --generic;
        }
        if (callee)
            --callee;
    }
    const auto *selected = symbol_at(doc, doc.tokens[callee].span.offset);
    if (!selected)
        return Json{};
    Json::Array signatures;
    for (const auto &s : symbols) {
        if (s.name != selected->name || s.parent != selected->parent || !s.declaration ||
            s.declaration->kind != Declaration::Kind::Function)
            continue;
        Json::Array params;
        for (const auto &p : s.declaration->parameters)
            params.emplace_back(Json{{"label", p.name + ": " + syntax_type(p.type)}});
        signatures.emplace_back(
            Json{{"label", s.detail},
                 {"documentation", Json{{"kind", "markdown"}, {"value", s.documentation}}},
                 {"parameters", params},
                 {"activeParameter", std::min(argument, static_cast<int>(params.size() ? params.size() - 1 : 0))}});
    }
    if (signatures.empty())
        return Json{};
    return Json{{"signatures", signatures}, {"activeSignature", 0}, {"activeParameter", argument}};
}
Json AnalysisSession::workspace_edit(const std::vector<DiagnosticEdit> &edits) const {
    std::map<std::string, Json::Array> by_file;
    for (const auto &edit : edits) {
        auto doc = documents.find(edit.span.file);
        if (doc == documents.end() || doc->second.library)
            continue;
        by_file[edit.span.file].emplace_back(
            Json{{"range", source_range(doc->second.source, edit.span)}, {"newText", edit.replacement}});
    }
    Json::Array changes;
    for (const auto &[path, file_edits] : by_file) {
        const auto &doc = documents.at(path);
        Json version = doc.open ? Json{doc.version} : Json{};
        changes.emplace_back(
            Json{{"textDocument", Json{{"uri", doc.uri}, {"version", version}}}, {"edits", file_edits}});
    }
    return Json{{"documentChanges", changes}};
}
Json AnalysisSession::code_actions(const EditorDocument &doc, const Json &params) const {
    Json::Array result;
    const auto begin = source_offset(doc.source, params["range"]["start"]),
               end = source_offset(doc.source, params["range"]["end"]);
    std::vector<DiagnosticEdit> all;
    for (const auto &d : diagnostics) {
        if (d.span.file != doc.path || !d.data())
            continue;
        for (const auto &suggestion : d.data()->suggestions) {
            bool applicable = true;
            for (const auto &edit : suggestion.edits) {
                auto target = documents.find(edit.span.file);
                if (target == documents.end() || target->second.library)
                    applicable = false;
            }
            if (!applicable)
                continue;
            if (d.span.offset <= end && d.span.offset + d.span.length >= begin)
                result.emplace_back(Json{{"title", suggestion.message},
                                         {"kind", "quickfix"},
                                         {"isPreferred", suggestion.applicability == Applicability::MachineApplicable},
                                         {"edit", workspace_edit(suggestion.edits)}});
            if (suggestion.applicability == Applicability::MachineApplicable)
                all.insert(all.end(), suggestion.edits.begin(), suggestion.edits.end());
        }
    }
    if (!all.empty()) {
        std::sort(all.begin(), all.end(), [](const auto &a, const auto &b) {
            return std::tie(a.span.file, a.span.offset) < std::tie(b.span.file, b.span.offset);
        });
        bool overlap = false;
        for (std::size_t i = 1; i < all.size(); ++i)
            if (all[i - 1].span.file == all[i].span.file &&
                all[i - 1].span.offset + all[i - 1].span.length > all[i].span.offset)
                overlap = true;
        if (!overlap)
            result.emplace_back(Json{{"title", "Fix all automatically applicable Neura diagnostics"},
                                     {"kind", "source.fixAll.neura"},
                                     {"edit", workspace_edit(all)}});
    }
    const auto *symbol = symbol_at(doc, begin);
    if (symbol && symbol->declaration && !symbol->declaration->documentation) {
        const auto &d = *symbol->declaration;
        std::string comment = "/// @brief " + d.name + ".\n";
        for (const auto &g : d.generics)
            comment += "/// @tparam " + g.name + " \n";
        for (const auto &p : d.parameters)
            comment += "/// @param " + p.name + " \n";
        if (d.kind == Declaration::Kind::Function && d.type && d.type->name != "void")
            comment += "/// @return \n";
        auto insertion = d.span;
        for (const auto &tag : d.tags)
            if (tag.span.offset < insertion.offset)
                insertion = tag.span;
        insertion.length = 0;
        auto line_start = insertion.offset;
        while (line_start && doc.source[line_start - 1] != '\n' && doc.source[line_start - 1] != '\r')
            --line_start;
        const auto prefix = doc.source.substr(line_start, insertion.offset - line_start);
        const bool inline_declaration = prefix.find_first_not_of(" \t") != std::string::npos;
        const auto first_code = prefix.find_first_not_of(" \t");
        auto indent = inline_declaration ? prefix.substr(0, first_code) : prefix;
        if (inline_declaration && symbol->parent != none)
            indent += "    ";
        if (!inline_declaration)
            insertion.offset = line_start;
        const auto newline_at = doc.source.find_first_of("\r\n");
        const std::string newline =
            newline_at != std::string::npos && doc.source[newline_at] == '\r'
                ? newline_at + 1 < doc.source.size() && doc.source[newline_at + 1] == '\n' ? "\r\n" : "\r"
                : "\n";
        std::string indented = inline_declaration ? newline : std::string{};
        std::istringstream lines(comment);
        for (std::string line; std::getline(lines, line);)
            indented += indent + line + newline;
        if (inline_declaration)
            indented += indent;
        result.emplace_back(Json{{"title", "Generate Neura documentation comment"},
                                 {"kind", "refactor.rewrite"},
                                 {"edit", workspace_edit({{insertion, indented}})}});
    }
    return result;
}
} // namespace neura::tooling
