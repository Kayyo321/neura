#include "analysis.hpp"

#include "cli/source_loader.hpp"
#include "cli/module_path.hpp"
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
// The documentation index keeps the original Doxygen text. Editor prose is
// Markdown, so render its commands rather than displaying their source syntax.
std::string documentation_markdown(std::string_view text) {
    std::string result;
    bool code = false, fence = false;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        const auto first = line.find_first_not_of(" \t");
        auto trimmed = first == std::string_view::npos ? std::string_view{} : line.substr(first);
        if (trimmed.starts_with("```"))
            fence = !fence;
        if (!fence && !trimmed.empty() && (trimmed.front() == '@' || trimmed.front() == '\\')) {
            const auto end = trimmed.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", 1);
            const auto command = trimmed.substr(1, end == std::string_view::npos ? end : end - 1);
            auto body = end == std::string_view::npos ? std::string_view{} : trimmed.substr(end);
            const auto content = body.find_first_not_of(" \t");
            body = content == std::string_view::npos ? std::string_view{} : body.substr(content);
            if (command == "endcode" || command == "endverbatim") {
                line = "```";
                code = false;
            } else if (!code && (command == "code" || command == "verbatim")) {
                line = "```";
                code = true;
            } else if (!code && (command == "brief" || command == "details"))
                line = body;
            else if (!code) {
                std::string rendered;
                if (command == "param" || command == "tparam" || command == "retval") {
                    if (command == "param" && body.starts_with('[')) {
                        const auto close = body.find(']');
                        if (close != std::string_view::npos) {
                            body.remove_prefix(close + 1);
                            const auto at = body.find_first_not_of(" \t");
                            body = at == std::string_view::npos ? std::string_view{} : body.substr(at);
                        }
                    }
                    const auto space = body.find_first_of(" \t");
                    rendered = "\n**" + std::string(command == "tparam" ? "Type parameter" : command == "retval" ? "Return value" : "Parameter") +
                               " `" + std::string(body.substr(0, space)) + "`**";
                    if (space != std::string_view::npos)
                        rendered += " — " + std::string(body.substr(space + 1));
                } else {
                    std::string title = command == "return" || command == "returns" ? "Returns" : std::string(command);
                    if (!title.empty()) title.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(title.front())));
                    rendered = "\n**" + title + "** " + std::string(body);
                }
                result += rendered;
                result += '\n';
                continue;
            }
        }
        result += line;
        result += '\n';
    }
    while (!result.empty() && result.back() == '\n') result.pop_back();
    if (code) result += "\n```";
    return result;
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
    const auto first = std::lower_bound(doc.tokens.begin(), doc.tokens.end(), span.offset,
                                        [](const Token &token, std::size_t offset) { return token.span.offset < offset; });
    for (auto it = first; it != doc.tokens.end(); ++it) {
        const auto &token = *it;
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

Json EditorDocument::position(std::size_t offset) const {
    offset = std::min(offset, source.size());
    if (line_starts.empty())
        return source_position(source, offset);
    const auto after = std::upper_bound(line_starts.begin(), line_starts.end(), offset);
    const auto line = static_cast<std::size_t>(after - line_starts.begin() - 1);
    const auto start = line_starts[line];
    auto result = source_position(std::string_view(source).substr(start, offset - start), offset - start);
    result["line"] = line + static_cast<std::size_t>(result["line"].integer());
    return result;
}
Json EditorDocument::range(const Span &span) const {
    return Json{{"start", position(span.offset)}, {"end", position(span.offset + span.length)}};
}

void AnalysisSession::parse_document(EditorDocument &document) {
    document.line_starts.assign(1, 0);
    for (std::size_t i = 0; i < document.source.size(); ++i) {
        if (document.source[i] == '\r') {
            if (i + 1 < document.source.size() && document.source[i + 1] == '\n')
                ++i;
            document.line_starts.push_back(i + 1);
        } else if (document.source[i] == '\n')
            document.line_starts.push_back(i + 1);
    }
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
        if (it->is_directory(error) && (name == ".git" || name == ".claude" || name == ".codex" ||
                                        name == ".worktrees" || name == "worktrees" || name == "out" ||
                                        name == "node_modules" || name == "build" || name.starts_with("build-") ||
                                        name.starts_with("cmake-build-") || name == "dist" || name == ".neura" ||
                                        name == ".neura-vscode" || name == "third_party")) {
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
    symbol_names_.clear();
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
    rebuild_symbol_index();
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
                                      ? doc.source.find_last_not_of(" \t\r\n", token.span.offset - 1)
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
            symbol.documentation = documentation_markdown(decl->documentation->text);
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
        const auto selected = d.name_span ? *d.name_span : named_span(doc, d.span, d.name);
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
    const auto candidates = symbol_names_.find({doc.path, std::string(name)});
    if (candidates == symbol_names_.end())
        return result;
    for (const auto i : candidates->second) {
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

void AnalysisSession::rebuild_symbol_index() {
    symbol_names_.clear();
    for (std::size_t i = 0; i < symbols.size(); ++i)
        symbol_names_[{symbols[i].file, symbols[i].name}].push_back(i);
    for (auto &[key, candidates] : symbol_names_)
        std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
            return std::tie(symbols[a].selection.offset, a) < std::tie(symbols[b].selection.offset, b);
        });
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
        const auto candidates = symbol_names_.find({original.span.file, original.name});
        const auto consider = [&](std::size_t i) {
            const auto &s = symbols[i];
            const auto delta = s.selection.offset > original.span.offset ? s.selection.offset - original.span.offset
                                                                         : original.span.offset - s.selection.offset;
            if (delta < distance || (delta == distance && (!best || i < *best))) {
                best = i;
                distance = delta;
            }
        };
        if (candidates != symbol_names_.end()) {
            const auto &indices = candidates->second;
            const auto next = std::lower_bound(indices.begin(), indices.end(), original.span.offset,
                [&](std::size_t i, std::size_t at) { return symbols[i].selection.offset < at; });
            if (next != indices.end())
                consider(*next);
            if (next != indices.begin()) {
                const auto previous_offset = symbols[*std::prev(next)].selection.offset;
                const auto previous = std::lower_bound(indices.begin(), next, previous_offset,
                    [&](std::size_t i, std::size_t at) { return symbols[i].selection.offset < at; });
                consider(*previous);
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
            auto &indices = symbol_names_[{original.span.file, original.name}];
            const auto next = std::lower_bound(indices.begin(), indices.end(), symbols[*best].selection.offset,
                [&](std::size_t i, std::size_t at) { return symbols[i].selection.offset < at; });
            indices.insert(next, *best);
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
        const auto first = std::lower_bound(doc->second.tokens.begin(), doc->second.tokens.end(), node.span.offset,
            [](const Token &token, std::size_t at) { return token.span.offset < at; });
        for (auto it = first; it != doc->second.tokens.end(); ++it) {
            const auto &token = *it;
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
    return Json{{"uri", found->second.uri}, {"range", found->second.range(span)}};
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
        Json item{{"range", doc.range(d.span)},
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
        const auto first = std::lower_bound(doc.tokens.begin(), doc.tokens.end(), t.span.offset,
            [](const Token &token, std::size_t at) { return token.span.offset < at; });
        for (auto it = first; it != doc.tokens.end() && it->span.offset < t.span.offset + t.span.length; ++it) {
            const auto &token = *it;
            if (token.kind == TokenKind::Identifier) {
                int kind = 1;
                if (auto id = resolve_name(doc, token.text, token.span.offset)) {
                    const auto &target = symbols[*id];
                    if (target.kind == 5 || target.kind == 10 || target.kind == 11 || target.kind == 26)
                        kind = target.token_type;
                }
                put(token.span, kind, 0, 1);
            }
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
        const auto start = doc.position(offset),
                   end = doc.position(offset + mark.span.length);
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

Json AnalysisSession::completion(const EditorDocument &doc, std::size_t offset, bool override_only) const {
    offset = std::min(offset, doc.source.size());
    Json::Array items;
    std::set<std::string> seen;
    if (override_only) {
        std::vector<std::string_view> words;
        for (const auto &token : doc.tokens)
            if (token.kind != TokenKind::End && token.span.offset + token.span.length <= offset)
                words.push_back(token.text);
        if (words.size() < 3 || words.back() != "override" || words[words.size() - 2] != "." ||
            (words[words.size() - 3] != "let" && words[words.size() - 3] != "const"))
            return Json{{"isIncomplete", false}, {"items", items}};
    }
    const auto identifier = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    std::size_t start = offset;
    while (start && identifier(doc.source[start - 1])) --start;
    const auto prefix = doc.source.substr(start, offset - start);
    const auto replacement_range = doc.range(Span{doc.path, 1, 1, start, offset - start});
    const auto before = start ? doc.source.find_last_not_of(" \t\r\n", start - 1) : std::string::npos;
    const auto add = [&](std::string name, int kind, std::string detail = {}, std::string documentation = {}) {
        if (name.empty() || name == "_" || name.starts_with('$') || !name.starts_with(prefix) ||
            !seen.insert(name + detail).second) return;
        items.emplace_back(Json{{"label", name}, {"kind", kind}, {"detail", detail},
                                {"documentation", Json{{"kind", "markdown"}, {"value", documentation}}},
                                {"textEdit", Json{{"range", replacement_range}, {"newText", name}}}});
    };
    // Do not offer language words inside comment trivia or string literals.
    for (const auto &token : doc.tokens) {
        if (token.kind == TokenKind::String && offset > token.span.offset &&
            offset < token.span.offset + token.span.length)
            return Json{{"isIncomplete", false}, {"items", items}};
    }
    const auto line_start = doc.source.rfind('\n', offset ? offset - 1 : 0);
    auto comment = doc.source.find("//", line_start == std::string::npos ? 0 : line_start + 1);
    while (comment != std::string::npos && comment < offset) {
        bool in_string = false;
        for (const auto &token : doc.tokens)
            if (token.kind == TokenKind::String && contains(token.span, comment)) in_string = true;
        if (!in_string) return Json{{"isIncomplete", false}, {"items", items}};
        comment = doc.source.find("//", comment + 2);
    }
    if (before != std::string::npos && doc.source[before] == '#') {
        for (const auto &tag : tag_registry()) {
            if (tag.status == TagStatus::Reserved) continue;
            add(std::string(tag.name), 14, tag_signature(tag) + " — " + std::string(tag_status_name(tag.status)),
                std::string(tag.meaning));
        }
        return Json{{"isIncomplete", false}, {"items", items}};
    }

    // Incomplete imports cannot be lowered. Enumerate actual source modules
    // using the compiler's filename convention, including unopened stdlib files.
    std::size_t qualified_start = start;
    while (qualified_start && (identifier(doc.source[qualified_start - 1]) || doc.source[qualified_start - 1] == '.'))
        --qualified_start;
    const auto qualified = doc.source.substr(qualified_start, offset - qualified_start);
    auto import_at = qualified_start ? doc.source.find_last_not_of(" \t\r\n", qualified_start - 1) : std::string::npos;
    bool importing = false;
    if (import_at != std::string::npos && doc.source[import_at] == ':') {
        auto word_end = import_at;
        while (word_end && std::isspace(static_cast<unsigned char>(doc.source[word_end - 1]))) --word_end;
        importing = word_end >= 6 && doc.source.substr(word_end - 6, 6) == "import" &&
                    (word_end == 6 || !identifier(doc.source[word_end - 7]));
    }
    const auto pascal = [](std::string_view text) {
        std::string name;
        bool upper = true;
        for (const char c : text) {
            if (c == '-') { upper = true; continue; }
            name += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            upper = false;
        }
        return name;
    };
    if (importing) {
        const auto dot = qualified.find_last_of('.');
        const auto parent = dot == std::string::npos ? std::string{} : qualified.substr(0, dot + 1);
        std::set<std::string> modules;
        if (std::string_view("Std").starts_with(qualified)) modules.insert("Std");
        const auto enumerate = [&](const fs::path &directory, bool standard) {
            std::error_code error;
            if (!fs::is_directory(directory, error)) return;
            for (fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, error), end;
                 !error && it != end; it.increment(error)) {
                const auto filename = it->path().filename().string();
                if (it->is_directory(error) && (filename.starts_with('.') || filename == "node_modules" ||
                    filename == "third_party" || filename == "dist" || filename == "out" ||
                    filename == "worktrees" || filename == "build" ||
                    filename.starts_with("build-") || filename.starts_with("cmake-build-"))) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(error) || it->path().extension() != ".nra") continue;
                const auto relative = it->path().lexically_relative(directory);
                std::string name = standard ? "Std" : "";
                for (auto part = relative.begin(); part != relative.end(); ++part)
                    name += (name.empty() ? "" : ".") + pascal(std::next(part) == relative.end()
                                                                 ? part->stem().string() : part->string());
                if ((name == "Std" || name.starts_with("Std.")) != standard) continue;
                if (!doc.library && (name.ends_with(".Internal") || name.find(".Internal.") != std::string::npos)) continue;
                if (module_path(name) != (standard ? fs::path("std") / relative : relative)) continue;
                modules.insert(std::move(name));
            }
        };
        if (qualified.empty() || qualified == "Std" || qualified.starts_with("Std."))
            enumerate(library / "std", true);
        if (!qualified.starts_with("Std.")) {
            enumerate(fs::path(doc.path).parent_path(), false);
            for (const auto &directory : source_roots) enumerate(directory, false);
            for (const auto &[path, open_doc] : documents) {
                if (!open_doc.open || open_doc.library) continue;
                for (const auto &statement : open_doc.syntax.statements)
                    if (statement.declaration && statement.declaration->kind == Declaration::Kind::Module)
                        modules.insert(statement.declaration->name);
            }
        }
        for (const auto &name : modules) {
            if (!name.starts_with(qualified)) continue;
            auto component = name.substr(parent.size());
            component = component.substr(0, component.find('.'));
            add(component, 9, "module " + parent + component);
        }
        return Json{{"isIncomplete", false}, {"items", items}};
    }

    struct Target {
        const Declaration *declaration{};
        const EditorDocument *document{};
        const Declaration *owner{};
        const TypeSyntax *annotation{};
        bool instance{};
    };
    const auto exported = [](Target target) {
        return target.declaration->exported || (target.owner &&
               (target.owner->kind == Declaration::Kind::Interface || target.owner->kind == Declaration::Kind::Enum));
    };
    std::map<std::string, EditorDocument> loaded;
    const auto find_declaration = [&](const EditorDocument &context, std::string_view name, std::size_t at) {
        Target result;
        std::size_t scope_size = std::numeric_limits<std::size_t>::max();
        std::function<void(const std::vector<Statement> &, const Span &)> statements;
        std::function<void(const Declaration &, const Span &)> declaration;
        declaration = [&](const Declaration &candidate, const Span &scope) {
            if (candidate.name == name && contains(scope, at) && scope.length <= scope_size &&
                (candidate.kind != Declaration::Kind::Binding || !candidate.name_span || candidate.name_span->offset <= at)) {
                result = {&candidate, &context};
                scope_size = scope.length;
            }
            if (!contains(candidate.span, at)) return;
            for (const auto &parameter : candidate.parameters)
                if (parameter.name == name && candidate.span.length <= scope_size) {
                    result = {nullptr, &context, &candidate, &parameter.type, true};
                    scope_size = candidate.span.length;
                }
            for (const auto &member : candidate.members) declaration(member, candidate.span);
            statements(candidate.body, candidate.span);
        };
        statements = [&](const std::vector<Statement> &body, const Span &scope) {
            for (const auto &statement : body) {
                if (statement.declaration) declaration(*statement.declaration, scope);
                const auto nested = statement.kind == Statement::Kind::Block ? statement.span : scope;
                statements(statement.body, nested);
                statements(statement.alternative, nested);
            }
        };
        statements(context.syntax.statements, Span{context.path, 1, 1, 0, context.source.size()});
        return result;
    };
    const auto imported = [&](std::string_view name, const EditorDocument &context) {
        Target result;
        if (name.ends_with(".*")) return result;
        if (!context.library && (name.ends_with(".Internal") || name.find(".Internal.") != std::string::npos)) return result;
        std::vector<fs::path> paths;
        if (name.starts_with("Std.")) paths.push_back(library / module_path(name));
        else {
            paths.push_back(fs::path(context.path).parent_path() / module_path(name));
            for (const auto &directory : source_roots) paths.push_back(directory / module_path(name));
        }
        for (auto path : paths) {
            std::error_code error;
            path = fs::weakly_canonical(path, error);
            if (error) continue;
            const auto key = path.generic_string();
            const EditorDocument *source = nullptr;
            if (const auto found = documents.find(key); found != documents.end()) source = &found->second;
            else if (const auto found = loaded.find(key); found != loaded.end()) source = &found->second;
            else if (fs::is_regular_file(path, error)) {
                try {
                    EditorDocument next;
                    next.path = key;
                    next.source = read_text(path);
                    next.library = name.starts_with("Std.");
                    next.tokens = lex_recovering(next.source, key, next.syntax_diagnostics);
                    next.syntax = parse_recovering(next.tokens, next.syntax_diagnostics);
                    source = &loaded.emplace(key, std::move(next)).first->second;
                } catch (const std::exception &) { continue; }
            }
            if (!source) continue;
            const auto separator = name.find_last_of('.');
            const auto component = name.substr(separator == std::string_view::npos ? 0 : separator + 1);
            for (const auto &statement : source->syntax.statements)
                if (statement.declaration && statement.declaration->kind == Declaration::Kind::Module &&
                    statement.declaration->name == component)
                    return Target{statement.declaration.get(), source};
        }
        return result;
    };
    std::function<Target(Target, unsigned)> nominal;
    std::function<Target(const EditorDocument &, std::string_view, std::size_t, unsigned)> resolve;
    std::function<std::vector<Target>(Target, unsigned)> members;
    members = [&](Target target, unsigned depth) {
        std::vector<Target> result;
        if (!target.declaration || depth > 32) return result;
        for (const auto &member : target.declaration->members)
            result.push_back({&member, target.document, target.declaration});
        const auto inherit = [&](const TypeSyntax &type) {
            auto base = resolve(*target.document, type.name, type.span.offset, depth + 1);
            auto inherited = members(base, depth + 1);
            for (const auto &item : inherited)
                if (exported(item) && !(item.declaration->static_member && item.owner &&
                    item.owner->kind == Declaration::Kind::Interface) &&
                    std::none_of(result.begin(), result.end(), [&](const Target &own) {
                    return own.declaration->name == item.declaration->name;
                })) result.push_back(item);
        };
        if (target.declaration->base) inherit(*target.declaration->base);
        for (const auto &interface : target.declaration->interfaces) inherit(interface);
        return result;
    };
    resolve = [&](const EditorDocument &context, std::string_view name, std::size_t at, unsigned depth) -> Target {
        if (depth > 32 || name.empty()) return {};
        const auto dot = name.find('.');
        auto target = nominal(find_declaration(context, name.substr(0, dot), at), depth + 1);
        if (dot == std::string_view::npos) return target;
        name.remove_prefix(dot + 1);
        while (target.declaration && !name.empty()) {
            const auto end = name.find('.');
            const auto component = name.substr(0, end);
            const auto children = members(target, depth + 1);
            const auto found = std::find_if(children.begin(), children.end(), [&](const Target &item) {
                return item.declaration->name == component && (exported(item) ||
                    (target.document->path == context.path && contains(target.declaration->span, at)));
            });
            if (found == children.end()) return {};
            target = nominal(*found, depth + 1);
            if (end == std::string_view::npos) return target;
            name.remove_prefix(end + 1);
        }
        return target;
    };
    nominal = [&](Target target, unsigned depth) -> Target {
        if (depth > 32) return {};
        if (target.annotation) {
            if (target.annotation->anonymous)
                return Target{target.annotation->anonymous.get(), target.document, nullptr, nullptr, true};
            auto type = resolve(*target.document, target.annotation->name, target.annotation->span.offset, depth + 1);
            type.instance = true;
            return type;
        }
        if (!target.declaration) return {};
        const auto &declaration = *target.declaration;
        if (declaration.kind != Declaration::Kind::Binding) return target;
        if (declaration.initializer && declaration.initializer->kind == Expr::Kind::Import)
            return imported(declaration.initializer->text, *target.document);
        if (declaration.type) {
            if (declaration.type->anonymous)
                return Target{declaration.type->anonymous.get(), target.document, nullptr, nullptr, true};
            auto type = resolve(*target.document, declaration.type->name, declaration.type->span.offset, depth + 1);
            type.instance = true;
            return type;
        }
        if (declaration.initializer) {
            const auto &value = *declaration.initializer;
            if (value.kind == Expr::Kind::Name)
                return resolve(*target.document, value.text, value.span.offset, depth + 1);
            if (value.kind == Expr::Kind::New && value.type) {
                auto type = resolve(*target.document, value.type->name, value.type->span.offset, depth + 1);
                type.instance = true;
                return type;
            }
        }
        return target;
    };
    Target enclosing;
    bool in_function = false;
    for (const auto &symbol : symbols)
        if (symbol.file == doc.path && symbol.declaration && contains(symbol.span, offset) &&
            (symbol.kind == 2 || symbol.kind == 5 || symbol.kind == 11) &&
            (!enclosing.declaration || symbol.span.length < enclosing.declaration->span.length))
            enclosing = {symbol.declaration, &doc};
    for (const auto &symbol : symbols)
        if (symbol.file == doc.path && symbol.declaration && symbol.declaration->kind == Declaration::Kind::Function &&
            contains(symbol.span, offset) && (!enclosing.declaration || symbol.span.length < enclosing.declaration->span.length))
            in_function = true;
    // Token context distinguishes declaration modifiers from ordinary members.
    std::vector<const Token *> preceding;
    for (const auto &token : doc.tokens)
        if (token.kind != TokenKind::End && token.span.offset + token.span.length <= start)
            preceding.push_back(&token);
    const auto text_before = [&](std::size_t reverse) -> std::string_view {
        return preceding.size() > reverse ? preceding[preceding.size() - 1 - reverse]->text : std::string_view{};
    };
    const bool modifier_context = text_before(0) == "." && (text_before(1) == "let" || text_before(1) == "const");
    if (modifier_context) {
        if (text_before(1) == "let" && !in_function) {
            if (!enclosing.declaration || enclosing.declaration->kind == Declaration::Kind::Module)
                add("promote", 14, "Bring an imported module's exports into this scope");
            if (enclosing.declaration && enclosing.declaration->kind == Declaration::Kind::Class)
                add("as", 14, "Declare a class cast");
        }
        if (!in_function && enclosing.declaration && (enclosing.declaration->kind == Declaration::Kind::Class ||
            enclosing.declaration->base || !enclosing.declaration->interfaces.empty())) {
            add("override", 14, "Replace an inherited member");
            if (!items.empty() && items.back()["label"].string() == "override") {
                items.back()["textEdit"]["newText"] = "override ";
                items.back()["command"] = Json{{"title", "Complete inherited member"}, {"command", "editor.action.triggerSuggest"}};
            }
        }
        return Json{{"isIncomplete", false}, {"items", items}};
    }
    const bool overriding = text_before(0) == "override" && text_before(1) == "." &&
                            (text_before(2) == "let" || text_before(2) == "const");
    const auto add_declaration = [&](Target target) {
        const auto &declaration = *target.declaration;
        if (!doc.library && target.document->library && std::any_of(declaration.tags.begin(), declaration.tags.end(),
            [](const TagSyntax &tag) { return tag.name == "Internal"; })) return;
        const auto kind = declaration.kind == Declaration::Kind::Function ? 3 :
                          declaration.kind == Declaration::Kind::Class ? 7 :
                          declaration.kind == Declaration::Kind::Interface ? 8 :
                          declaration.kind == Declaration::Kind::Module ? 9 :
                          declaration.kind == Declaration::Kind::Enum ? 13 : 6;
        add(declaration.name, kind, signature(declaration),
            declaration.documentation ? documentation_markdown(declaration.documentation->text) : "");
    };
    if (overriding) {
        if (enclosing.declaration) {
            const auto &container = *enclosing.declaration;
            auto inherited = members(enclosing, 0);
            for (const auto &target : inherited) {
                const auto &candidate = *target.declaration;
                // A new nominal definition has a new type identity, so it
                // cannot replace its inherited namesake. Binding aliases can
                // preserve that identity and remain valid override candidates.
                if ((candidate.kind != Declaration::Kind::Binding && candidate.kind != Declaration::Kind::Function) ||
                    !exported(target) || candidate.static_member || !candidate.generics.empty() ||
                    !candidate.overloads.empty() || candidate.cast_target || candidate.name == "init" || candidate.name == "deinit" ||
                    std::any_of(container.members.begin(), container.members.end(), [&](const Declaration &own) {
                        return own.name == candidate.name;
                    }) || std::any_of(candidate.tags.begin(), candidate.tags.end(), [](const TagSyntax &tag) {
                        return tag.name == "Final";
                    })) continue;
                if (container.kind == Declaration::Kind::Class && candidate.kind != Declaration::Kind::Function) continue;
                add_declaration(target);
            }
            if (container.kind == Declaration::Kind::Class)
                for (const auto &[name, detail] : {
                    std::pair{"equals", "equals(other: class) -> bool"},
                    std::pair{"hash_code", "hash_code() -> uint32"},
                    std::pair{"to_string", "to_string() -> string"}}) {
                    if (std::none_of(inherited.begin(), inherited.end(), [&](const Target &candidate) {
                        return candidate.declaration->name == name;
                    })) add(name, 3, detail);
                }
        }
        return Json{{"isIncomplete", false}, {"items", items}};
    }
    const bool member = before != std::string::npos && doc.source[before] == '.';
    if (member) {
        std::size_t end = before;
        while (end && std::isspace(static_cast<unsigned char>(doc.source[end - 1]))) --end;
        std::size_t begin = end;
        while (begin && (identifier(doc.source[begin - 1]) || doc.source[begin - 1] == '.')) --begin;
        const auto receiver_name = std::string_view(doc.source).substr(begin, end - begin);
        const auto canonical = [](std::string name) {
            if (name == "int") return std::string("int32");
            if (name == "byte") return std::string("uint8");
            if (name == "float") return std::string("float32");
            return name;
        };
        // These are the primitive_wrapper contracts in lowering.cpp. Source
        // declarations supply the available functions and their documentation;
        // a value consumes the first parameter as its receiver.
        const std::map<std::string, std::string> wrappers{
            {"bool", "Bool"}, {"string", "String"}, {"int8", "Int8"}, {"int16", "Int16"},
            {"int32", "Int32"}, {"int64", "Int64"}, {"uint8", "UInt8"}, {"uint16", "UInt16"},
            {"uint32", "UInt32"}, {"uint64", "UInt64"}, {"float16", "Float16"},
            {"bfloat16", "BFloat16"}, {"float32", "Float32"}, {"float64", "Float64"}};
        struct ValueType { TypeSyntax syntax; bool value{true}; };
        std::function<std::optional<ValueType>(const Expr &, const EditorDocument &, unsigned)> expression_type;
        std::function<std::optional<ValueType>(Target, unsigned)> declared_type;
        declared_type = [&](Target candidate, unsigned depth) -> std::optional<ValueType> {
            if (depth > 32) return {};
            if (candidate.annotation) return ValueType{*candidate.annotation};
            if (!candidate.declaration) return {};
            if (candidate.declaration->kind != Declaration::Kind::Binding) return {};
            if (candidate.declaration->type) return ValueType{*candidate.declaration->type};
            if (candidate.declaration->initializer)
                return expression_type(*candidate.declaration->initializer, *candidate.document, depth + 1);
            return {};
        };
        expression_type = [&](const Expr &value, const EditorDocument &context, unsigned depth) -> std::optional<ValueType> {
            if (depth > 32) return {};
            if (value.kind == Expr::Kind::Name) {
                const auto candidate = find_declaration(context, value.text, value.span.offset);
                if (!candidate.declaration && !candidate.annotation && wrappers.contains(canonical(value.text))) {
                    TypeSyntax type;
                    type.name = canonical(value.text);
                    return ValueType{std::move(type), false};
                }
                return declared_type(candidate, depth + 1);
            }
            if (value.kind == Expr::Kind::Cast && value.type) return ValueType{*value.type};
            if (value.kind == Expr::Kind::Unary && value.operands.size() == 1)
                return expression_type(value.operands.front(), context, depth + 1);
            TypeSyntax type;
            switch (value.kind) {
            case Expr::Kind::Integer: type.name = "int32"; break;
            case Expr::Kind::Float: type.name = "float64"; break;
            case Expr::Kind::String: type.name = "string"; break;
            case Expr::Kind::Boolean: type.name = "bool"; break;
            case Expr::Kind::Collection: {
                if (value.operands.empty()) return {};
                auto element = expression_type(value.operands.front(), context, depth + 1);
                if (!element) return {};
                for (const auto &item : value.operands) {
                    const auto next = expression_type(item, context, depth + 1);
                    if (!next || syntax_type(next->syntax) != syntax_type(element->syntax)) return {};
                }
                element->syntax.collections.push_back(CollectionKind::Array);
                return element;
            }
            default: return {};
            }
            return ValueType{std::move(type)};
        };
        auto type = declared_type(find_declaration(doc, receiver_name, begin), 0);
        if (!type && wrappers.contains(canonical(std::string(receiver_name)))) {
            TypeSyntax syntax;
            syntax.name = canonical(std::string(receiver_name));
            type = ValueType{std::move(syntax), false};
        }
        if (type) {
            const auto name = canonical(type->syntax.name);
            const bool collection = !type->syntax.collections.empty();
            const auto wrapper = wrappers.find(name);
            if (collection || wrapper != wrappers.end()) {
                // Composite intrinsic operations are available only when the
                // element itself supports value members (aggregate.hpp).
                bool value_members = wrapper != wrappers.end();
                if (collection && !value_members) {
                    const auto element = resolve(doc, type->syntax.name, begin, 0);
                    value_members = element.declaration && (element.declaration->kind == Declaration::Kind::Class ||
                        element.declaration->kind == Declaration::Kind::Interface ||
                        (element.declaration->kind == Declaration::Kind::Enum && std::none_of(
                            element.declaration->members.begin(), element.declaration->members.end(),
                            [](const Declaration &variant) { return !variant.parameters.empty(); })));
                }
                if (type->value && value_members) {
                    add("equals", 3, "equals(other: " + syntax_type(type->syntax) + ") -> bool");
                    add("hash_code", 3, "hash_code() -> uint32");
                    add("to_string", 3, "to_string() -> string");
                }
                if (collection) {
                    add("length", 10, "length: int32");
                    if (type->syntax.collections.back() == CollectionKind::List) {
                        auto element = type->syntax;
                        element.collections.pop_back();
                        add("version", 10, "version: uint64");
                        add("append", 3, "append(value: " + syntax_type(element) + ") -> void");
                        add("remove_at", 3, "remove_at(index: int32) -> void");
                    }
                } else {
                    if (type->value && name == "string") {
                        add("byte_length", 10, "byte_length: int32");
                        add("byte_at", 3, "byte_at(index: int32) -> uint8");
                        add("slice", 3, "slice(start: int32, end: int32) -> string");
                    }
                    if (type->value && (name.starts_with("float") || name == "bfloat16"))
                        add("to_bits", 3, "to_bits() -> uint" + std::string(name == "bfloat16" ? "16" : name.substr(5)));
                    const auto companion = imported("Std.Core." + wrapper->second, doc);
                    for (const auto &candidate : members(companion, 0)) {
                        const auto &method = *candidate.declaration;
                        if (!exported(candidate) || method.kind != Declaration::Kind::Function) continue;
                        if (type->value && (method.parameters.empty() || !method.parameters.front().type.collections.empty() ||
                            canonical(method.parameters.front().type.name) != name || method.parameters.front().variadic ||
                            method.name == "equals" || method.name == "hash_code" || method.name == "to_string" ||
                            method.name == "to_bits" || method.name == "byte_at" || method.name == "slice")) continue;
                        Declaration signature_source{};
                        signature_source.kind = Declaration::Kind::Function;
                        signature_source.name = method.name;
                        signature_source.exported = method.exported;
                        signature_source.type = method.type;
                        signature_source.parameters = method.parameters;
                        if (type->value) signature_source.parameters.erase(signature_source.parameters.begin());
                        add(method.name, 3, signature(signature_source), method.documentation
                            ? documentation_markdown(method.documentation->text) : "");
                    }
                }
                return Json{{"isIncomplete", false}, {"items", items}};
            }
        }
        const auto target = resolve(doc, receiver_name, begin, 0);
        if (target.declaration && target.declaration->kind != Declaration::Kind::Binding) {
            const auto children = members(target, 0);
            for (const auto &candidate : children)
                if ((target.declaration->kind != Declaration::Kind::Class ||
                    (candidate.declaration->name != "init" && candidate.declaration->name != "deinit" &&
                     (target.instance || target.declaration->singleton || candidate.declaration->static_member))) &&
                    (target.declaration->kind != Declaration::Kind::Interface ||
                     candidate.declaration->static_member != target.instance) &&
                    (exported(candidate) || (candidate.document->path == doc.path &&
                    contains(target.declaration->span, offset)))) add_declaration(candidate);
            if (target.declaration->kind == Declaration::Kind::Class && (target.instance || target.declaration->singleton))
                for (const auto &[name, detail] : {
                    std::pair{"equals", "equals(other: class) -> bool"},
                    std::pair{"hash_code", "hash_code() -> uint32"},
                    std::pair{"to_string", "to_string() -> string"}}) {
                    if (std::none_of(children.begin(), children.end(), [&](const Target &candidate) {
                        return candidate.declaration->name == name;
                    })) add(name, 3, detail);
                }
            return Json{{"isIncomplete", false}, {"items", items}};
        }
    }
    std::optional<SymbolId> owner;
    if (member && program) {
        std::size_t begin = before;
        while (begin && std::isspace(static_cast<unsigned char>(doc.source[begin - 1]))) --begin;
        while (begin && identifier(doc.source[begin - 1])) --begin;
        if (const auto *receiver = symbol_at(doc, begin); receiver && receiver->compiler_id != no_symbol) {
            const auto &target = program->symbols[receiver->compiler_id];
            if (target.kind == SymbolKind::Module || target.kind == SymbolKind::Class ||
                target.kind == SymbolKind::Interface || target.kind == SymbolKind::Enum) owner = receiver->compiler_id;
            else if (target.type.symbol != no_symbol) owner = target.type.symbol;
            else if (target.base != no_symbol) owner = target.base;
        }
    }
    for (const auto &symbol : symbols) {
        if (member) {
            if (!owner || !program || symbol.compiler_id == no_symbol) continue;
            const auto &candidate = program->symbols[symbol.compiler_id];
            bool owned = candidate.owner == *owner;
            if (!owned && candidate.owner != no_symbol && is_static_scope(program->symbols[candidate.owner]))
                owned = program->symbols[candidate.owner].owner == *owner;
            if (!owned || !candidate.exported) continue;
        } else if (symbol.file != doc.path || !contains(symbol.scope, offset) ||
                   (symbol.kind == 13 && symbol.token_type != 6 && symbol.selection.offset > offset)) continue;
        add(symbol.name, symbol.kind == 12 || symbol.kind == 6 ? 3 : symbol.kind == 5 ? 7 :
                         symbol.kind == 11 ? 8 : symbol.kind == 2 ? 9 : symbol.kind == 10 ? 13 :
                         symbol.kind == 22 ? 20 : symbol.kind == 7 || symbol.kind == 8 ? 10 : 6,
            symbol.detail, symbol.documentation);
        if (!items.empty() && items.back()["label"].string() == symbol.name &&
            (symbol.modifiers & modifier("deprecated"))) items.back()["tags"] = Json::Array{1};
    }
    if (!member) {
        // Promoted imports contribute unqualified names. Local declarations win;
        // ambiguous exports from two modules do not form usable suggestions.
        std::map<std::string, std::pair<std::size_t, std::vector<Target>>> promoted;
        for (const auto &symbol : symbols) {
            if (symbol.file != doc.path || !symbol.declaration || !symbol.declaration->promote_import ||
                !contains(symbol.scope, offset) || !symbol.declaration->initializer) continue;
            const auto &name = symbol.declaration->initializer->text;
            std::vector<Target> modules;
            if (name.ends_with(".*")) {
                const auto prefix_name = name.substr(0, name.size() - 2);
                auto relative = module_path(prefix_name);
                relative.replace_extension();
                std::vector<fs::path> directories;
                if (name.starts_with("Std.")) directories.push_back(library / relative);
                else {
                    directories.push_back(fs::path(doc.path).parent_path() / relative);
                    for (const auto &directory : source_roots) directories.push_back(directory / relative);
                }
                std::set<std::string> names;
                for (const auto &directory : directories) {
                    std::error_code error;
                    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
                        if (it->is_regular_file(error) && it->path().extension() == ".nra") {
                            const auto module = prefix_name + "." + pascal(it->path().stem().string());
                            if (module_path(module) == relative / it->path().filename()) names.insert(module);
                        }
                }
                for (const auto &module : names) modules.push_back(imported(module, doc));
            } else modules.push_back(imported(name, doc));
            for (const auto &module : modules)
                for (const auto &candidate : members(module, 0)) {
                    if (!exported(candidate)) continue;
                    auto [entry, inserted] = promoted.try_emplace(candidate.declaration->name,
                                                                symbol.scope.length, std::vector<Target>{});
                    if (symbol.scope.length < entry->second.first) entry->second = {symbol.scope.length, {}};
                    auto &candidates = entry->second.second;
                    if (symbol.scope.length == entry->second.first && std::none_of(candidates.begin(), candidates.end(),
                        [&](const Target &old) { return old.declaration == candidate.declaration; }))
                        candidates.push_back(candidate);
                }
        }
        for (const auto &[name, entry] : promoted) {
            const auto &candidates = entry.second;
            if (candidates.size() != 1 || resolve_name(doc, name, offset)) continue;
            add_declaration(candidates.front());
        }
        for (const auto &word : {"let", "last", "const", "func", "class", "interface", "module", "enum", "if", "else",
                                 "while", "for", "return", "match", "scope", "spawn", "select", "new", "nil", "true",
                                 "false", "export", "import"}) add(word, 14);
    }
    return Json{{"isIncomplete", false}, {"items", items}};
}

Json AnalysisSession::document_symbols(const EditorDocument &doc) const {
    std::map<std::size_t, std::vector<std::size_t>> children;
    const auto visible = [&](const EditorSymbol &symbol) {
        return symbol.file == doc.path && !symbol.name.empty() && !symbol.name.starts_with('$') &&
               !(symbol.name == "value" && symbol.selection.length != 5);
    };
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const auto &s = symbols[i];
        if (!visible(s)) continue;
        auto parent = s.parent;
        // Anonymous functions have no outline name, but their named locals
        // still belong under the nearest named declaration.
        while (parent != none && !visible(symbols[parent])) parent = symbols[parent].parent;
        children[parent].push_back(i);
    }
    std::function<Json(std::size_t)> item = [&](std::size_t index) {
        const auto &s = symbols[index];
        Json::Array nested;
        for (auto child : children[index])
            nested.push_back(item(child));
        auto full = s.span;
        const auto begin = std::min(full.offset, s.selection.offset);
        const auto end = std::max(full.offset + full.length, s.selection.offset + s.selection.length);
        full.offset = begin;
        full.length = end - begin;
        Json result{{"name", s.name},
                    {"detail", s.detail},
                    {"kind", s.kind},
                    {"range", doc.range(full)},
                    {"selectionRange", doc.range(s.selection)}};
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
                                     {"uri", location(symbol.selection)["uri"]},
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
        result.emplace_back(Json{{"position", doc.position(s.selection.offset + s.selection.length)},
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
                Json{{"position", doc.position(offset)},
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
                result.emplace_back(Json{{"position", doc.position(argument.span.offset)},
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
                    result.emplace_back(Json{{"position", doc.position(node.span.offset)},
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
            Json{{"range", doc->second.range(edit.span)}, {"newText", edit.replacement}});
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
        if (!d.code().empty() && d.span.offset <= end && d.span.offset + d.span.length >= begin) {
            const auto title = "Explain " + std::string(d.code());
            result.emplace_back(Json{{"title", title}, {"kind", "quickfix"},
                                     {"command", Json{{"title", title}, {"command", "neura.explainDiagnostic"},
                                                       {"arguments", Json::Array{std::string(d.code())}}}}});
        }
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
