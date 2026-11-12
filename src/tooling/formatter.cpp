#include "formatter.hpp"

#include "lexer/lexer.hpp"
#include "parser/syntax.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace neura::tooling {
namespace {

namespace fs = std::filesystem;

struct Item {
    std::string_view text;
    std::size_t offset{};
    std::size_t line{};
    unsigned lines_before{};
    bool comment{};
};

// The compiler owns token recognition. Ordinary comments are recovered only
// from the gaps between compiler tokens; strings can never be mistaken for
// comments and no second grammar is introduced here.
std::vector<Item> with_trivia(std::string_view source, const std::vector<Token>& tokens) {
    std::vector<Item> items;
    std::size_t cursor = source.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    std::size_t line = 1;
    unsigned lines = 0;
    for (const auto& token : tokens) {
        while (cursor < token.span.offset) {
            if (source[cursor] == '\r' || source[cursor] == '\n') {
                if (source[cursor] == '\r' && cursor + 1 < token.span.offset && source[cursor + 1] == '\n') ++cursor;
                ++cursor;
                ++line;
                ++lines;
            } else if (source[cursor] == '/' && cursor + 1 < token.span.offset && source[cursor + 1] == '/') {
                const auto start = cursor;
                while (cursor < token.span.offset && source[cursor] != '\r' && source[cursor] != '\n') ++cursor;
                items.push_back({source.substr(start, cursor - start), start, line, lines, true});
                lines = 0;
            } else {
                ++cursor;
            }
        }
        if (token.kind != TokenKind::End) {
            items.push_back({source.substr(token.span.offset, token.span.length), token.span.offset,
                             token.span.line, lines, false});
            cursor = token.span.offset + token.span.length;
            line = token.span.line;
            lines = 0;
        }
    }
    return items;
}

struct Hints {
    std::set<std::size_t> bodies;
    std::set<std::size_t> inline_braces;
    std::set<std::size_t> angles;
    std::set<std::size_t> prefix;
    std::set<std::size_t> postfix;
    std::set<std::size_t> glued;
    std::set<std::size_t> accessors;
    std::set<std::size_t> before;
    std::set<std::size_t> blank_before;
    std::map<std::size_t, std::size_t> accessor_ends;
};

class SyntaxHints {
public:
    SyntaxHints(const std::vector<Token>& tokens, std::string_view source)
        : tokens_(tokens), source_(source) {}

    Hints collect(const ParsedFile& file) {
        for (const auto& item : file.statements) statement(item);
        return std::move(hints_);
    }

private:
    const std::vector<Token>& tokens_;
    std::string_view source_;
    Hints hints_;

    template<class F> void in_span(const Span& span, F apply) {
        const auto first = std::lower_bound(tokens_.begin(), tokens_.end(), span.offset,
            [](const Token& token, std::size_t offset) { return token.span.offset < offset; });
        for (auto token = first; token != tokens_.end() && token->span.offset < span.offset + span.length; ++token) apply(*token);
    }

    void angle_span(const Span& span) {
        in_span(span, [&](const Token& token) {
            if (token.text == "<" || token.text == ">") hints_.angles.insert(token.span.offset);
        });
    }

    void type(const TypeSyntax& value) {
        if (value.anonymous) declaration(*value.anonymous);
        else angle_span(value.span);
        if (value.reference) hints_.prefix.insert(value.span.offset);
        for (const auto& argument : value.arguments) type(argument);
    }

    void types(const std::vector<TypeSyntax>& values) {
        for (const auto& value : values) type(value);
    }

    static bool expands(const TypeSyntax& value) {
        if (value.anonymous && !value.anonymous->members.empty()) return true;
        return std::any_of(value.arguments.begin(), value.arguments.end(), [](const auto& argument) { return expands(argument); });
    }

    static bool expands(const Expr& value) {
        if (value.lambda || value.match || (value.type && expands(*value.type))) return true;
        if (std::any_of(value.type_arguments.begin(), value.type_arguments.end(), [](const auto& argument) { return expands(argument); })) return true;
        return std::any_of(value.operands.begin(), value.operands.end(), [](const auto& operand) { return expands(operand); });
    }

    void expression(const Expr& value) {
        if (value.type) type(*value.type);
        types(value.type_arguments);
        if (!value.type_arguments.empty() && !value.operands.empty()) {
            const auto start = value.operands.front().span.offset + value.operands.front().span.length;
            const auto end = value.open ? value.open->offset : value.span.offset + value.span.length;
            if (end > start) angle_span({{}, 1, 1, start, end - start});
        }
        if (value.kind == Expr::Kind::Unary || value.kind == Expr::Kind::PrefixUpdate)
            hints_.prefix.insert(value.span.offset);
        if (value.kind == Expr::Kind::PostfixUpdate && !value.operands.empty()) {
            const auto operand_end = value.operands.front().span.offset + value.operands.front().span.length;
            in_span(value.span, [&](const Token& token) {
                if (token.span.offset >= operand_end && token.text == value.text) hints_.postfix.insert(token.span.offset);
            });
        }
        if (value.kind == Expr::Kind::Binary && (value.text == "<<" || value.text == ">>") && value.operands.size() == 2) {
            const auto start = value.operands[0].span.offset + value.operands[0].span.length;
            const auto end = value.operands[1].span.offset;
            bool first = true;
            in_span({{}, 1, 1, start, end - start}, [&](const Token& token) {
                if (token.text == value.text.substr(0, 1)) {
                    if (!first) hints_.glued.insert(token.span.offset);
                    first = false;
                }
            });
        }
        if (value.kind == Expr::Kind::NamedVariant) {
            const auto start = value.operands.empty() ? value.span.offset :
                value.operands.front().span.offset + value.operands.front().span.length;
            for (const auto& token : tokens_) {
                if (token.span.offset >= start && token.span.offset < value.span.offset + value.span.length && token.text == "{") {
                    hints_.inline_braces.insert(token.span.offset);
                    break;
                }
            }
        }
        if (value.lambda) declaration(*value.lambda);
        if (value.match) statement(*value.match);
        for (const auto& operand : value.operands) expression(operand);
    }

    std::size_t first_offset(const Declaration& value) const {
        std::size_t result = value.span.offset;
        for (const auto& tag : value.tags) result = std::min(result, tag.span.offset);
        if (value.documentation) result = std::min(result, value.documentation->span.offset);
        return result;
    }

    void declaration(const Declaration& value, bool inline_tags = false) {
        if (value.named_payload) {
            bool found = false;
            in_span(value.span, [&](const Token& token) {
                if (!found && token.text == "{") {
                    hints_.inline_braces.insert(token.span.offset);
                    found = true;
                }
            });
        }
        if (value.type) type(*value.type);
        if (value.base) type(*value.base);
        if (value.cast_target) type(*value.cast_target);
        types(value.interfaces);
        if (value.initializer) expression(*value.initializer);
        for (const auto& generic : value.generics) types(generic.bounds);
        for (const auto& parameter : value.parameters) type(parameter.type);

        // Generic declaration delimiters and the enclosing member-list
        // parenthesis are identified from parsed declarations, not capitalization.
        const bool container = value.kind == Declaration::Kind::Class || value.kind == Declaration::Kind::Module ||
                               value.kind == Declaration::Kind::Interface || value.kind == Declaration::Kind::Enum;
        bool head = false;
        bool found_body = false;
        in_span(value.span, [&](const Token& token) {
            if (!head && (token.text == "class" || token.text == "module" || token.text == "interface" ||
                          token.text == "enum" || token.text == "singleton" || token.text == "func")) head = true;
            else if (head && !found_body) {
                if (token.text == "<" || token.text == ">") hints_.angles.insert(token.span.offset);
                if (token.text == "(") {
                    found_body = true;
                    const bool multiline_enum = value.kind != Declaration::Kind::Enum ||
                        source_.substr(value.span.offset, value.span.length).find_first_of("\r\n") != std::string_view::npos;
                    if (container && multiline_enum) hints_.bodies.insert(token.span.offset);
                }
                if (token.text == "{") found_body = true;
            }
        });

        for (std::size_t i = 0; i < value.members.size(); ++i) {
            const auto& member = value.members[i];
            if (i > 0) {
                const auto& previous = value.members[i - 1];
                const bool multiline = previous.kind != Declaration::Kind::Binding ||
                    (previous.type && expands(*previous.type)) ||
                    (previous.initializer && expands(*previous.initializer)) ||
                    source_.substr(previous.span.offset, previous.span.length).find_first_of("\r\n") != std::string_view::npos;
                if (multiline || !previous.accessors.empty()) hints_.blank_before.insert(first_offset(member));
            }
            declaration(member, value.kind == Declaration::Kind::Enum);
        }
        for (const auto& body : value.body) statement(body);
        for (const auto& overload : value.overloads) declaration(overload);
        for (const auto& accessor : value.accessors) {
            hints_.accessors.insert(accessor.span.offset);
            hints_.accessor_ends.insert_or_assign(accessor.span.offset + accessor.span.length, accessor.span.offset);
            if (accessor.expression) expression(*accessor.expression);
            for (const auto& body : accessor.body) statement(body);
        }
        // Standalone declaration tags stay on their own lines. Parameter
        // and enum-variant tags are deliberately left inline.
        if (!inline_tags) {
            const auto start = first_offset(value);
            for (const auto& token : tokens_) {
                if (token.span.offset < start) continue;
                if (token.span.offset >= value.span.offset) break;
                if (token.text == "#") hints_.before.insert(token.span.offset);
            }
            if (!value.tags.empty()) hints_.before.insert(value.span.offset);
        }
    }

    void statement(const Statement& value) {
        if (value.declaration) declaration(*value.declaration);
        if (value.binding_type) type(*value.binding_type);
        for (const auto& item : value.expressions) expression(item);
        for (const auto& item : value.body) statement(item);
        for (const auto& item : value.alternative) statement(item);
        for (const auto& item : value.initializer) statement(item);
        for (const auto& item : value.step) statement(item);
        for (const auto& arm : value.arms) {
            if (arm.named) {
                bool found = false;
                in_span(arm.span, [&](const Token& token) {
                    if (!found && token.text == "{") {
                        hints_.inline_braces.insert(token.span.offset);
                        found = true;
                    }
                });
            }
            for (const auto& item : arm.body) statement(item);
            if (arm.value) expression(*arm.value);
        }
        for (const auto& arm : value.select_arms) {
            if (arm.operation) expression(*arm.operation);
            for (const auto& item : arm.body) statement(item);
        }
    }
};

bool word(std::string_view text) {
    if (text.empty()) return false;
    const auto first = static_cast<unsigned char>(text.front());
    return (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') ||
           (first >= '0' && first <= '9') || first == '_' || first == '"';
}

class Writer {
public:
    Writer(FormatOptions options, std::string eol, bool bom)
        : options_(options), eol_(std::move(eol)) {
        if (bom) output_ = "\xEF\xBB\xBF";
    }

    void line(unsigned count = 1) { pending_ = std::max(pending_, count); }
    void suppress_line() { pending_ = 0; }
    bool pending() const { return pending_ > 0; }

    void text(std::string_view value, unsigned depth, bool space) {
        flush();
        if (at_start_) {
            output_ += options_.insert_spaces ? std::string(static_cast<std::size_t>(depth) * options_.tab_size, ' ')
                                             : std::string(depth, '\t');
            at_start_ = false;
        } else if (space && !output_.empty() && output_.back() != ' ' && output_.back() != '\t') {
            output_ += ' ';
        }
        output_ += value;
    }

    std::string finish() {
        pending_ = 1;
        flush();
        return std::move(output_);
    }

private:
    FormatOptions options_;
    std::string eol_;
    std::string output_;
    unsigned pending_{};
    bool at_start_{true};

    void flush() {
        if (pending_ == 0) return;
        if (!at_start_) {
            output_ += eol_;
            at_start_ = true;
            if (pending_ > 1) output_ += eol_;
        }
        pending_ = 0;
    }
};

struct Delimiter {
    std::string_view open;
    bool multiline{};
    bool control{};
};

std::string render(std::string_view source, const std::vector<Token>& tokens, const Hints& hints, FormatOptions options) {
    auto eol = std::string("\n");
    const auto first_eol = source.find_first_of("\r\n");
    if (first_eol != std::string_view::npos && source[first_eol] == '\r')
        eol = first_eol + 1 < source.size() && source[first_eol + 1] == '\n' ? "\r\n" : "\r";
    Writer out(options, std::move(eol), source.starts_with("\xEF\xBB\xBF"));
    const auto items = with_trivia(source, tokens);
    std::vector<Delimiter> delimiters;
    unsigned depth = 0;
    bool accessor = false;
    std::size_t accessor_end = 0;
    const Item* previous = nullptr;

    const auto in_inline = [&] { return !delimiters.empty() && !delimiters.back().multiline; };
    const auto indent = [&] { return depth + (accessor ? 1U : 0U); };
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const auto text = item.text;
        const auto before = previous ? previous->text : std::string_view{};
        const auto next = i + 1 < items.size() ? items[i + 1].text : std::string_view{};
        if (accessor && item.offset >= accessor_end) accessor = false;
        if (item.comment) {
            const bool trailing = previous && !previous->comment && item.line == previous->line;
            if (trailing) out.suppress_line();
            else if (hints.blank_before.contains(item.offset)) out.line(2);
            else out.line(item.lines_before > 1 ? 2 : 1);
            out.text(text, indent(), trailing);
            out.line();
            previous = &item;
            continue;
        }
        if (hints.accessors.contains(item.offset)) {
            accessor = true;
            out.line();
            for (const auto& [end, start] : hints.accessor_ends) {
                if (start == item.offset) { accessor_end = end; break; }
            }
        }
        if (hints.blank_before.contains(item.offset)) out.line(2);
        else if (hints.before.contains(item.offset)) out.line();
        // Keep deliberate blank lines and wrapped expression lines. At most
        // one blank line survives, and closing delimiters stay structural.
        if (item.lines_before > 1) out.line(2);
        else if (item.lines_before && in_inline() && text != ")" && text != "]" && text != "}") out.line();

        bool space = previous && !previous->comment;
        if (text == ")" || text == "]" || text == "," || text == ";" || text == "." || text == ":") space = false;
        if (before == "(" || before == "[" || before == "." || before == "#") space = false;
        if (hints.angles.contains(item.offset) || (previous && hints.angles.contains(previous->offset) && before == "<")) space = false;
        if (hints.glued.contains(item.offset) || hints.postfix.contains(item.offset) ||
            (previous && hints.prefix.contains(previous->offset))) space = false;
        if (text == "[") space = before == "=" || before == "return" || before == "in" || before == "=>" ||
                                 before == ":" || before == "let" || before == "const";
        if (before == ":" && previous) {
            const auto previous_index = i > 1 ? i - 2 : 0;
            const auto colon_owner = i > 1 ? items[previous_index].text : std::string_view{};
            if (colon_owner == "import" || colon_owner == "module" || colon_owner == "class" || colon_owner == "interface") space = false;
        }
        if (text == "<" && hints.angles.contains(item.offset)) {
            out.text(text, indent(), false);
            delimiters.push_back({text, false, false});
        } else if (text == ">" && hints.angles.contains(item.offset)) {
            if (!delimiters.empty() && delimiters.back().open == "<") delimiters.pop_back();
            out.text(text, indent(), false);
        } else if (text == "(" ) {
            const bool control = before == "if" || before == "while" || before == "for" || before == "match" || before == "scope";
            const bool body = hints.bodies.contains(item.offset);
            space = control || body || before == "module" || before == "class" || before == "interface" || before == "enum" || before == "singleton";
            out.text(text, indent(), space);
            const bool multiline = body && next != ")";
            delimiters.push_back({text, multiline, control});
            if (multiline) { ++depth; out.line(); }
        } else if (text == "{") {
            const bool inline_brace = hints.inline_braces.contains(item.offset);
            const bool multiline = !inline_brace && next != "}";
            out.text(text, indent(), true);
            delimiters.push_back({text, multiline, false});
            if (multiline) { ++depth; out.line(); }
        } else if (text == "[") {
            out.text(text, indent(), space);
            delimiters.push_back({text, false, false});
        } else if (text == ")" || text == "]" || text == "}") {
            bool multiline = false;
            if (!delimiters.empty()) {
                multiline = delimiters.back().multiline;
                delimiters.pop_back();
            }
            if (multiline) { --depth; out.line(); }
            out.text(text, indent(), text == "}" && !multiline && before != "{");
            if (text == "}" && next != ";" && next != "," && next != ")" && next != "]" && next != "." &&
                next != "(" && next != "else" && next != "while" && next != "or" && next != "callback" &&
                next != "=>" && next != "+" && next != "-" && next != "*" && next != "/" && next != "==" && next != "?" && next != ":") out.line();
        } else {
            // Retain the lexical distinction between a binary operator and
            // a unary operator, and between adjacent operator tokens.
            if (previous && !space && !hints.glued.contains(item.offset) && !hints.angles.contains(item.offset) &&
                !hints.prefix.contains(previous->offset) && !hints.postfix.contains(item.offset) && word(before) && word(text)) space = true;
            out.text(text, indent(), space);
            if (text == ";") {
                if (!in_inline()) out.line();
            } else if (text == ",") {
                if (!delimiters.empty() && delimiters.back().multiline) out.line();
            }
        }
        previous = &item;
    }
    return out.finish();
}

std::string read(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + file.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write(const fs::path& file, const std::string& original, const std::string& formatted) {
    if (fs::is_symlink(file)) throw std::runtime_error("refusing to replace symbolic link " + file.string());
    if (read(file) != original) throw std::runtime_error("source changed during formatting: " + file.string());
    auto temporary = file;
#ifndef _WIN32
    temporary += ".neura-fmt-XXXXXX";
    auto name = temporary.string();
    std::vector<char> buffer(name.begin(), name.end());
    buffer.push_back('\0');
    const int descriptor = ::mkstemp(buffer.data());
    if (descriptor < 0) throw std::runtime_error("cannot create formatting temporary file beside " + file.string());
    ::close(descriptor);
    temporary = buffer.data();
#else
    temporary += ".neura-fmt.tmp";
    if (fs::exists(temporary)) throw std::runtime_error("formatting temporary file already exists: " + temporary.string());
#endif
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot write " + temporary.string());
            output.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
            output.close();
            if (!output) throw std::runtime_error("failed writing " + temporary.string());
        }
        fs::permissions(temporary, fs::status(file).permissions());
        if (read(file) != original) throw std::runtime_error("source changed during formatting: " + file.string());
        fs::rename(temporary, file);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

} // namespace

std::string format_source(std::string_view source, const std::string& file, FormatOptions options) {
    if (options.tab_size == 0 || options.tab_size > 16) throw std::invalid_argument("format tab size must be between 1 and 16");
    const auto tokens = lex(source, file);
    const auto syntax = parse(tokens);
    const auto hints = SyntaxHints(tokens, source).collect(syntax);
    auto formatted = render(source, tokens, hints, options);
    const auto result = lex(formatted, file);
    if (tokens.size() != result.size()) throw std::runtime_error("formatter refused a change to source tokenization");
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto& before = tokens[i];
        const auto& after = result[i];
        if (before.kind != after.kind || before.text != after.text)
            throw std::runtime_error("formatter refused a change to source tokenization");
        if (static_cast<bool>(before.documentation) != static_cast<bool>(after.documentation) ||
            (before.documentation && before.documentation->text != after.documentation->text))
            throw std::runtime_error("formatter refused a change to documentation attachment");
    }
    (void)parse(result);
    return formatted;
}

int run_formatter(const std::vector<std::string>& arguments) {
    bool check = false;
    bool inplace = false;
    bool stdin_source = false;
    bool paths_only = false;
    std::string stdin_path = "<stdin>.nra";
    FormatOptions options;
    std::vector<fs::path> files;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        const auto value = [&]() -> const std::string& {
            if (i + 1 >= arguments.size()) throw std::invalid_argument(argument + " needs a value");
            return arguments[++i];
        };
        if (paths_only) files.emplace_back(argument);
        else if (argument == "--") paths_only = true;
        else if (argument == "--check") check = true;
        else if (argument == "--write") inplace = true;
        else if (argument == "--stdin") stdin_source = true;
        else if (argument == "--stdin-filepath") { stdin_path = value(); stdin_source = true; }
        else if (argument == "--use-tabs") options.insert_spaces = false;
        else if (argument == "--tab-size") {
            const auto& number = value();
            const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), options.tab_size);
            if (error != std::errc{} || end != number.data() + number.size() || options.tab_size == 0 || options.tab_size > 16)
                throw std::invalid_argument("--tab-size expects an integer between 1 and 16");
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "usage: neura fmt [--check | --write] [--tab-size N] [--use-tabs] <file.nra>...\n"
                         "       neura fmt [--check] --stdin [--stdin-filepath <file.nra>]\n"
                         "Without --write or --check, print one formatted source. No files means stdin.\n";
            return 0;
        } else if (!argument.empty() && argument.front() == '-') throw std::invalid_argument("unknown fmt option: " + argument);
        else files.emplace_back(argument);
    }
    if (check && inplace) throw std::invalid_argument("fmt --check and --write cannot be combined");
    if (stdin_source && !files.empty()) throw std::invalid_argument("fmt stdin input cannot be combined with source paths");
    if (files.empty()) stdin_source = true;
    if (stdin_source && inplace) throw std::invalid_argument("fmt --write requires source paths");
    if (!check && !inplace && files.size() > 1) throw std::invalid_argument("fmt multiple files requires --check or --write");
    struct Source { fs::path file; std::string original; std::string formatted; };
    std::vector<Source> sources;
    if (stdin_source) {
        std::string original{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
        sources.push_back({stdin_path, std::move(original), {}});
    } else {
        for (const auto& file : files) sources.push_back({file, read(file), {}});
    }
    // Validate the whole batch before changing the first file.
    for (auto& source : sources) source.formatted = format_source(source.original, source.file.string(), options);
    bool changed = false;
    for (const auto& source : sources) {
        const bool different = source.original != source.formatted;
        changed |= different;
        if (check && different) std::cerr << source.file.string() << " needs formatting\n";
        else if (inplace && different) write(source.file, source.original, source.formatted);
        else if (!check && !inplace) std::cout << source.formatted;
    }
    return check && changed ? 1 : 0;
}

} // namespace neura::tooling
