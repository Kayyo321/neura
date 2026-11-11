#include "source_docs.hpp"

#include "parser/syntax.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace neura {
namespace {

using DeclKind = Declaration::Kind;

void quote(std::ostream& output, std::string_view value) {
    constexpr std::string_view hex = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value) {
        if (byte == '"' || byte == '\\') {
            output.put('\\');
            output.put(static_cast<char>(byte));
        } else if (byte < 32) {
            output << "\\u00" << hex[byte >> 4] << hex[byte & 15];
        } else {
            output.put(static_cast<char>(byte));
        }
    }
    output.put('"');
}

void range(std::ostream& output, const Span& span) {
    output << "{\"line\":" << span.line << ",\"column\":" << span.column
           << ",\"offset\":" << span.offset << ",\"length\":" << span.length << '}';
}

std::string_view trim_start(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string_view{} : text.substr(first);
}

struct Tag {
    std::string_view command;
    std::string_view subject;
    std::string_view direction;
    std::string text;
};

std::optional<Tag> tag(std::string_view line) {
    if (!line.starts_with('@') && !line.starts_with('\\')) return std::nullopt;
    line.remove_prefix(1);
    constexpr std::string_view letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const auto end = line.find_first_not_of(letters);
    const auto command = line.substr(0, end);
    if (command.empty()) return std::nullopt;
    auto arguments = end == std::string_view::npos ? std::string_view{} : trim_start(line.substr(end));
    std::string_view direction;
    if (command == "param" && arguments.starts_with('[')) {
        const auto close = arguments.find(']');
        if (close != std::string_view::npos) {
            direction = arguments.substr(1, close - 1);
            arguments = trim_start(arguments.substr(close + 1));
        }
    }
    std::string_view subject;
    if (command == "param" || command == "tparam" || command == "retval") {
        const auto separator = arguments.find_first_of(" \t");
        subject = arguments.substr(0, separator);
        arguments = separator == std::string_view::npos ? std::string_view{}
                                                        : trim_start(arguments.substr(separator));
    }
    return Tag{command, subject, direction, std::string(arguments)};
}

void write_comment(std::ostream& output, const DocComment& comment) {
    std::vector<Tag> tags;
    std::string_view remaining = comment.text;
    std::string_view literal_end;
    while (!remaining.empty()) {
        const auto newline = remaining.find('\n');
        const auto line = remaining.substr(0, newline);
        remaining = newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1);
        const auto trimmed = trim_start(line);
        if (literal_end == "fence") {
            if (trimmed.starts_with("```")) literal_end = {};
        } else if (auto command = tag(trimmed); command && (literal_end.empty() || command->command == literal_end)) {
            if (command->command == "code") literal_end = "endcode";
            else if (command->command == "verbatim") literal_end = "endverbatim";
            else literal_end = {};
            tags.push_back(std::move(*command));
            continue;
        } else if (literal_end.empty() && trimmed.starts_with("```")) {
            literal_end = "fence";
        }
        if (!tags.empty()) {
            tags.back().text += '\n';
            tags.back().text += line;
        }
    }
    output << "{\"text\":";
    quote(output, comment.text);
    output << ",\"range\":";
    range(output, comment.span);
    output << ",\"tags\":[";
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i != 0) output.put(',');
        const auto& item = tags[i];
        output << "{\"command\":";
        quote(output, item.command);
        if (!item.subject.empty()) {
            output << ",\"subject\":";
            quote(output, item.subject);
        }
        if (!item.direction.empty()) {
            output << ",\"direction\":";
            quote(output, item.direction);
        }
        output << ",\"text\":";
        quote(output, item.text);
        output.put('}');
    }
    output << "]}";
}

std::string_view argument_kind(TagArgumentSyntax::Kind kind) {
    switch (kind) {
    case TagArgumentSyntax::Kind::String: return "string";
    case TagArgumentSyntax::Kind::Integer: return "integer";
    case TagArgumentSyntax::Kind::Float: return "float";
    case TagArgumentSyntax::Kind::Boolean: return "bool";
    case TagArgumentSyntax::Kind::Name: return "name";
    case TagArgumentSyntax::Kind::List: return "list";
    }
    return {};
}

void write_argument(std::ostream& output, const TagArgumentSyntax& argument) {
    output << "{\"kind\":";
    quote(output, argument_kind(argument.kind));
    if (!argument.name.empty()) {
        output << ",\"name\":";
        quote(output, argument.name);
    }
    if (argument.kind == TagArgumentSyntax::Kind::List) {
        output << ",\"items\":[";
        for (std::size_t i = 0; i < argument.items.size(); ++i) {
            if (i != 0) output.put(',');
            write_argument(output, argument.items[i]);
        }
        output.put(']');
    } else {
        output << ",\"value\":";
        quote(output, argument.text);
    }
    output.put('}');
}

// The tags written before a declaration (docs/lang/Tags.md), as written.
void write_tags(std::ostream& output, const std::vector<TagSyntax>& tags) {
    output.put('[');
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i != 0) output.put(',');
        output << "{\"name\":";
        quote(output, tags[i].name);
        output << ",\"range\":";
        range(output, tags[i].span);
        output << ",\"arguments\":[";
        for (std::size_t j = 0; j < tags[i].arguments.size(); ++j) {
            if (j != 0) output.put(',');
            write_argument(output, tags[i].arguments[j]);
        }
        output << "]}";
    }
    output.put(']');
}

bool hidden(const Declaration& declaration) {
    return std::ranges::any_of(declaration.tags, [](const TagSyntax& tag) { return tag.name == "Hidden"; });
}

std::string_view kind_name(const Declaration& declaration, bool variant) {
    if (variant) return "variant";
    if (declaration.cast_target) return "cast";
    if (declaration.singleton) return "singleton";
    switch (declaration.kind) {
    case DeclKind::Binding: return declaration.constant ? "constant" : "binding";
    case DeclKind::Function: return "function";
    case DeclKind::Class: return "class";
    case DeclKind::Module: return "module";
    case DeclKind::Enum: return "enum";
    case DeclKind::Interface: return "interface";
    }
    return {};
}

class SourceIndex {
public:
    SourceIndex(std::ostream& output, std::string_view source) : output_(output), source_(source) {}

    void statements(const std::vector<Statement>& statements) {
        for (const auto& statement : statements) this->statement(statement);
    }

private:
    static bool has_anonymous_class(const TypeSyntax& type) {
        return type.anonymous || std::ranges::any_of(type.arguments, has_anonymous_class);
    }

    std::string type_text(const TypeSyntax& type) const {
        if (type.name == "void" && type.collections.empty() && !type.reference) return "void";
        if (type.name == "[variadic argument]") return {};
        if (!has_anonymous_class(type)) return std::string(source_.substr(type.span.offset, type.span.length));
        // An anonymous class is summarized by its fields, as diagnostics
        // print it: class(x: int, y: string).
        std::string result = type.reference ? "&" : "";
        const auto list = [&](const std::vector<TypeSyntax>& types, std::size_t count) {
            for (std::size_t i = 0; i < count; ++i) result += (i == 0 ? "" : ", ") + type_text(types[i]);
        };
        if (type.anonymous) {
            const auto& body = *type.anonymous;
            result += "class";
            if (body.base) result += ":" + type_text(*body.base);
            if (!body.interfaces.empty()) {
                result += " implements ";
                list(body.interfaces, body.interfaces.size());
            }
            result += '(';
            bool first = true;
            for (const auto& member : body.members) {
                if (member.kind != DeclKind::Binding || member.static_member || member.cast_target) continue;
                result += std::string(first ? "" : ", ") +
                          (member.last ? "last " : member.constant ? "const " : "") + member.name;
                if (member.type) result += ": " + type_text(*member.type);
                result += accessor_text(member);
                first = false;
            }
            result += ')';
        } else if (type.name == "[tuple]") {
            result += '[';
            list(type.arguments, type.arguments.size());
            result += ']';
        } else if (type.name == "[function]") {
            result += "func(";
            list(type.arguments, type.arguments.size() - 1);
            result += ") -> " + type_text(type.arguments.back());
        } else {
            result += type.name + '<';
            list(type.arguments, type.arguments.size());
            result += '>';
        }
        for (const auto suffix : type.collections) result += suffix == CollectionKind::Array ? "[]" : "[list]";
        return result;
    }

    // A field's accessor clauses (Types.md, Accessors), getter first.
    static std::string accessor_text(const Declaration& declaration) {
        const auto has = [&](bool setter) {
            return std::ranges::any_of(declaration.accessors,
                                       [&](const AccessorClause& clause) { return clause.setter == setter; });
        };
        if (!has(false) && !has(true)) return {};
        return has(false) ? (has(true) ? " { get; set }" : " { get }") : " { set }";
    }

    std::string signature(const Declaration& declaration, std::string_view kind) const {
        std::string result;
        if (kind == "variant") {
            result = declaration.name;
        } else if (kind == "cast") {
            result = "let.as ";
            result += type_text(*declaration.cast_target);
            result += " = func";
        } else if (declaration.kind == DeclKind::Binding) {
            result = declaration.last ? "last " : declaration.constant ? "const " : "let ";
            result += declaration.name;
            if (declaration.type) {
                result += ": ";
                result += type_text(*declaration.type);
            }
            result += accessor_text(declaration);
            return result;
        } else {
            result = declaration.name + " = ";
            result += declaration.kind == DeclKind::Function ? "func" : kind;
        }
        if (!declaration.generics.empty()) {
            result += '<';
            for (std::size_t i = 0; i < declaration.generics.size(); ++i) {
                if (i != 0) result += ", ";
                const auto& generic = declaration.generics[i];
                result += source_.substr(generic.span.offset, generic.span.length);
            }
            result += '>';
        }
        if (declaration.base) {
            result += ':';
            result += type_text(*declaration.base);
        }
        if (!declaration.interfaces.empty()) {
            result += declaration.kind == DeclKind::Class ? " implements " : ":";
            for (std::size_t i = 0; i < declaration.interfaces.size(); ++i) {
                if (i != 0) result += ", ";
                result += type_text(declaration.interfaces[i]);
            }
        }
        if (declaration.kind == DeclKind::Function || (kind == "variant" && !declaration.parameters.empty())) {
            result += declaration.named_payload ? '{' : '(';
            for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
                if (i != 0) result += ", ";
                const auto& parameter = declaration.parameters[i];
                if (!parameter.name.empty()) result += parameter.name + ": ";
                if (parameter.variadic) result += "...";
                result += type_text(parameter.type);
            }
            result += declaration.named_payload ? '}' : ')';
        }
        if (declaration.kind == DeclKind::Function && declaration.type) {
            result += " -> ";
            result += type_text(*declaration.type);
        }
        return result;
    }

    void symbol(std::string_view name, std::string_view kind, std::string_view signature,
                const Span& declaration, const Span& selection, const DocComment& comment,
                const std::vector<TagSyntax>& tags = {}) {
        if (!first_) output_ << ",\n";
        first_ = false;
        output_ << "{\"name\":";
        quote(output_, name);
        output_ << ",\"kind\":";
        quote(output_, kind);
        output_ << ",\"signature\":";
        quote(output_, signature);
        output_ << ",\"scope\":[";
        for (std::size_t i = 0; i < scope_.size(); ++i) {
            if (i != 0) output_.put(',');
            quote(output_, scope_[i]);
        }
        output_ << "],\"declaration\":";
        range(output_, declaration);
        output_ << ",\"selection\":";
        range(output_, selection);
        output_ << ",\"documentation\":";
        write_comment(output_, comment);
        output_ << ",\"tags\":";
        write_tags(output_, tags);
        output_.put('}');
    }

    void declaration(const Declaration& declaration, bool variant = false) {
        // #Hidden leaves the declaration and everything in it out.
        if (hidden(declaration)) return;
        if (declaration.documentation && declaration.name_span && declaration.name != "_") {
            const auto kind = kind_name(declaration, variant);
            symbol(declaration.name, kind, signature(declaration, kind), declaration.span,
                   *declaration.name_span, *declaration.documentation, declaration.tags);
        }
        if (!declaration.name.empty()) scope_.push_back(declaration.name);
        for (const auto& member : declaration.members) {
            this->declaration(member, declaration.kind == DeclKind::Enum);
        }
        if (declaration.initializer) expression(*declaration.initializer);
        statements(declaration.body);
        for (const auto& clause : declaration.accessors) {
            if (clause.expression) expression(*clause.expression);
            statements(clause.body);
        }
        if (!declaration.name.empty()) scope_.pop_back();
        for (const auto& overload : declaration.overloads) this->declaration(overload);
    }

    void expression(const Expr& expression) {
        if (expression.lambda) declaration(*expression.lambda);
        if (expression.match) statement(*expression.match);
        for (const auto& operand : expression.operands) this->expression(operand);
    }

    void statement(const Statement& statement) {
        if (statement.declaration) declaration(*statement.declaration);
        if (statement.kind == Statement::Kind::Destructure && statement.documentation) {
            for (std::size_t i = 0; i < statement.bindings.size(); ++i) {
                const auto& binding = statement.bindings[i];
                if (binding.text == "_") continue;
                const auto kind = statement.constant_bindings ? "constant" : "binding";
                const auto prefix = statement.constant_bindings ? "const " : "let ";
                // A typed destructuring declaration gives each name its element type.
                const auto& annotation = statement.binding_type;
                const auto typed = annotation && i < annotation->arguments.size();
                symbol(binding.text, kind,
                       prefix + binding.text + (typed ? ": " + type_text(annotation->arguments[i]) : std::string()),
                       statement.span, binding.span, *statement.documentation);
            }
        }
        statements(statement.initializer);
        for (const auto& input : statement.expressions) expression(input);
        statements(statement.step);
        statements(statement.body);
        statements(statement.alternative);
        for (const auto& arm : statement.arms) {
            if (arm.value) expression(*arm.value);
            statements(arm.body);
        }
    }

    std::ostream& output_;
    std::string_view source_;
    std::vector<std::string_view> scope_;
    bool first_ = true;
};

std::string doxygen_type(const TypeSyntax& type);

std::string doxygen_types(const std::vector<TypeSyntax>& types, std::size_t count) {
    std::string result;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) result += ", ";
        result += doxygen_type(types[i]);
    }
    return result;
}

std::string doxygen_type(const TypeSyntax& type) {
    std::string result;
    if (type.name == "[tuple]") {
        result = "NeuraTuple<" + doxygen_types(type.arguments, type.arguments.size()) + '>';
    } else if (type.name == "[function]") {
        result = "NeuraFunction<" + doxygen_type(type.arguments.back()) + '(' +
            doxygen_types(type.arguments, type.arguments.size() - 1) + ")>";
    } else if (type.name == "[variadic argument]") {
        result = "NeuraValue";
    } else if (type.name == "class" || type.anonymous) {
        // An anonymous class has no name to give Doxygen.
        result = "NeuraObject";
    } else {
        for (const auto character : type.name) {
            if (character == '.') result += "::";
            else result += character;
        }
        if (!type.arguments.empty()) {
            result += '<' + doxygen_types(type.arguments, type.arguments.size()) + '>';
        }
    }
    for (const auto suffix : type.collections) {
        result = (suffix == CollectionKind::Array ? "NeuraArray<" : "NeuraList<") + result + '>';
    }
    if (type.reference) result += '&';
    return result;
}

std::string doxygen_parameters(const Declaration& decl) {
    std::string result = "(";
    for (std::size_t i = 0; i < decl.parameters.size(); ++i) {
        if (i != 0) result += ", ";
        const auto& param = decl.parameters[i];
        auto type = doxygen_type(param.type);
        if (param.variadic) type = "NeuraVariadic<" + type + '>';
        result += type;
        if (!param.name.empty()) result += ' ' + param.name;
    }
    result += ')';
    return result;
}

class DoxygenFilter {
public:
    explicit DoxygenFilter(std::string_view source) : source_(source) {
        for (std::size_t i = 0; i < source_.size(); ++i) {
            if (source_[i] == '\r' && i + 1 < source_.size() && source_[i + 1] == '\n') ++i;
            if (source_[i] == '\r' || source_[i] == '\n') offsets_.push_back(i + 1);
        }
        lines_.resize(offsets_.size());
    }

    void write(std::ostream& output, const ParsedFile& file) {
        for (const auto& statement : file.statements) {
            if (statement.declaration) declaration(*statement.declaration);
        }
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            if (i != 0) output.put('\n');
            output << lines_[i];
        }
    }

private:
    std::size_t line_at(std::size_t offset) const {
        return static_cast<std::size_t>(std::ranges::upper_bound(offsets_, offset) - offsets_.begin());
    }

    void append(std::size_t line, std::string_view text) {
        auto& output = lines_.at(line - 1);
        if (!output.empty()) output += ' ';
        output += text;
    }

    void comment(const DocComment& doc) {
        const auto limit = doc.span.offset + doc.span.length;
        for (std::size_t line = doc.span.line; offsets_[line - 1] < limit; ++line) {
            auto text = source_.substr(offsets_[line - 1], limit - offsets_[line - 1]);
            append(line, text.substr(0, text.find_first_of("\r\n")));
            if (line == offsets_.size()) break;
        }
    }

    std::string generics(const Declaration& decl) const {
        if (decl.generics.empty()) return {};
        std::string result = "template<";
        for (std::size_t i = 0; i < decl.generics.size(); ++i) {
            if (i != 0) result += ", ";
            result += "typename " + decl.generics[i].name;
        }
        return result + "> ";
    }

    void declaration(const Declaration& decl, bool in_class = false, std::string_view enumeration = {}) {
        if (decl.name == "_" || (decl.initializer && decl.initializer->kind == Expr::Kind::Import)) return;
        // An overload's syntax span starts after `or`, which can be on
        // its own line. Put its stub at the connector so the docs stay adjacent.
        const auto line = decl.documentation
            ? line_at(decl.documentation->span.offset + decl.documentation->span.length - 1) + 1
            : decl.span.line;
        if (in_class) {
            const auto access_line = decl.documentation ? decl.documentation->span.line - 1 : line;
            append(access_line, decl.exported ? "public:" : "private:");
        }
        if (decl.documentation) comment(*decl.documentation);
        auto stub = generics(decl);
        if (!enumeration.empty()) {
            stub = decl.parameters.empty() ? "static const " : "static ";
            stub += enumeration;
            stub += ' ' + decl.name;
            if (!decl.parameters.empty()) stub += doxygen_parameters(decl);
            append(line, stub + ';');
            return;
        }
        if (decl.kind == DeclKind::Binding) {
            if (decl.static_member) stub += "static ";
            if (decl.constant) stub += "const ";
            stub += decl.type ? doxygen_type(*decl.type) : "NeuraValue";
            append(line, stub + ' ' + decl.name + ';');
            return;
        }
        if (decl.kind == DeclKind::Function) {
            if (decl.static_member) stub += "static ";
            if (decl.cast_target) {
                stub += "operator " + doxygen_type(*decl.cast_target);
            } else {
                stub += doxygen_type(*decl.type) + ' ' + decl.name;
            }
            append(line, stub + doxygen_parameters(decl) + ';');
            for (const auto& overload : decl.overloads) declaration(overload, in_class);
            return;
        }

        const bool module = decl.kind == DeclKind::Module && !in_class;
        const bool is_enum = decl.kind == DeclKind::Enum;
        const bool class_type = decl.kind == DeclKind::Class || decl.kind == DeclKind::Interface;
        stub += (module ? "namespace " : "struct ") + decl.name;
        if (class_type) {
            bool first = true;
            const auto inherit = [&](const TypeSyntax& type) {
                stub += (first ? " : public " : ", public ") + doxygen_type(type);
                first = false;
            };
            if (decl.base) inherit(*decl.base);
            for (const auto& type : decl.interfaces) inherit(type);
        }
        append(line, stub + " {");
        for (const auto& child : decl.members) {
            declaration(child, decl.kind == DeclKind::Class, is_enum ? std::string_view(decl.name) : "");
        }
        const auto end = decl.span.offset + decl.span.length - 1;
        append(line_at(end), module ? "}" : "};");
    }

    std::string_view source_;
    std::vector<std::size_t> offsets_{0};
    std::vector<std::string> lines_;
};

std::string read_source(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + file.string());
    std::string source(std::istreambuf_iterator<char>(input), {});
    if (input.bad()) throw std::runtime_error("error reading " + file.string());
    return source;
}

} // namespace

void write_source_docs(std::ostream& output, const std::vector<std::filesystem::path>& files) {
    output << "{\"version\":1,\"files\":[";
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        const auto source = read_source(file);
        const auto path = file.generic_string();
        const auto syntax = parse(lex(source, path));
        if (i != 0) output << ",\n";
        output << "{\"path\":";
        quote(output, path);
        output << ",\"symbols\":[\n";
        SourceIndex(output, source).statements(syntax.statements);
        output << "\n]}";
    }
    output << "]}\n";
}

void write_doxygen_source(std::ostream& output, const std::filesystem::path& file) {
    const auto source = read_source(file);
    const auto syntax = parse(lex(source, file.generic_string()));
    DoxygenFilter(source).write(output, syntax);
}

} // namespace neura
