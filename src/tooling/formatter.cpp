#include "formatter.hpp"

#include "lexer/island.hpp"
#include "lexer/lexer.hpp"
#include "parser/html_template.hpp"
#include "parser/syntax.hpp"
#include "util/line_endings.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "platform/error.hpp"
#include "platform/file.hpp"
#include "platform/windows.hpp"
#else
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
    TokenKind kind{TokenKind::Symbol};
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
                items.push_back({source.substr(start, cursor - start), start, line, lines, true, TokenKind::Symbol});
                lines = 0;
            } else {
                ++cursor;
            }
        }
        if (token.kind != TokenKind::End) {
            items.push_back({source.substr(token.span.offset, token.span.length), token.span.offset,
                             token.span.line, lines, false, token.kind});
            cursor = token.span.offset + token.span.length;
            line = token.span.line;
            lines = 0;
        }
    }
    return items;
}

struct Hints {
    std::set<std::size_t> bodies;
    // The word `unless` of a return guard (docs/design/Expressions030.md),
    // which stays on the line of a '}' before it.
    std::set<std::size_t> guards;
    std::set<std::size_t> inline_braces;
    std::set<std::size_t> angles;
    std::set<std::size_t> prefix;
    std::set<std::size_t> postfix;
    std::set<std::size_t> glued;
    std::set<std::size_t> accessors;
    std::set<std::size_t> before;
    std::set<std::size_t> blank_before;
    std::map<std::size_t, std::size_t> accessor_ends;
    // The ':' of a conditional or of a range's step, which is spaced on
    // both sides, unlike a label's or an annotation's.
    std::set<std::size_t> spaced_colons;
    // The '(' of a record's components, which is spaced like a class body's,
    // and the ')' of a multiline record that needs a trailing ',' before it.
    std::set<std::size_t> record_bodies;
    std::set<std::size_t> trailing_commas;
    // The opening delimiters whose items are separated by ',' one per line
    // when the delimiter is laid out over several lines: an enum's or a
    // record's components, the arms of a match or a select, and the
    // replacements of a copy. A ',' in any other delimiter, such as a
    // statement block, never ends a line.
    std::set<std::size_t> comma_lists;
    // The '}' that ends a do-while body, which `while` follows on its line.
    std::set<std::size_t> do_closes;
    // The ':' before a declaration's base, written without spaces, as in
    // `class:Base (` and `interface<T>:Iterable<T> (`.
    std::set<std::size_t> base_colons;
    // The first token of an `or as` cast alternative, which is a member of
    // its own but continues the declaration before it.
    std::set<std::size_t> alternatives;
    // The '(' after the word `scope` of a scope statement, which is spaced
    // like a control statement's; a member named `scope` is called as any.
    std::set<std::size_t> scope_parens;
    // The '(' of every module, class, interface, enum and record body,
    // spaced from what comes before it even when it stays on one line.
    std::set<std::size_t> container_opens;
    // The '(' of an enum's variants: laid out over several lines, it keeps
    // the variants on the lines they are written on.
    std::set<std::size_t> packed_lists;
    // The '{' of a braced range, written without inner spaces: `{0 .. n}`.
    std::set<std::size_t> tight_braces;
    // The '(' of a `using` statement, spaced like the one after `scope`;
    // `using` is a contextual word, so its calls keep no space.
    std::set<std::size_t> control_parens;
    // The '(' that begins the value of a `yield`, which keeps its space.
    std::set<std::size_t> keyword_operands;
    // The first token of a `defer` statement's call, which never begins a
    // wrapped line: `defer` and its call are joined when split.
    std::set<std::size_t> deferred_calls;
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

    // The index of the first token at or after `offset`.
    std::size_t index_at(std::size_t offset) const {
        return static_cast<std::size_t>(std::lower_bound(tokens_.begin(), tokens_.end(), offset,
            [](const Token& token, std::size_t at) { return token.span.offset < at; }) - tokens_.begin());
    }

    // The '{' or '(' that a match, a select or a copy opens at or after
    // `offset`, and the '}' or ')' that closes it.
    std::pair<const Token*, const Token*> delimited(std::size_t offset, std::string_view open, std::string_view close) const {
        std::size_t depth = 0;
        const Token* first = nullptr;
        for (auto i = index_at(offset); i < tokens_.size(); ++i) {
            const auto& token = tokens_[i];
            if (token.text == open) {
                if (!first) first = &token;
                ++depth;
            } else if (token.text == close && first) {
                if (--depth == 0) return {first, &token};
            }
        }
        return {first, nullptr};
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

    // Whether a declaration, written on one line, is laid out over several.
    static bool expands(const Declaration& value) {
        switch (value.kind) {
        case Declaration::Kind::Class:
        case Declaration::Kind::Module:
        case Declaration::Kind::Interface:
            if (!value.record) return !value.members.empty() || !value.hooks.empty() || !value.startup_blocks.empty();
            return false;
        case Declaration::Kind::Enum:
            return false;
        default:
            break;
        }
        if (value.type && expands(*value.type)) return true;
        if (value.initializer && expands(*value.initializer)) return true;
        if (value.kind == Declaration::Kind::Function && value.has_body && !value.set_shorthand) {
            if (!value.expression_body && !value.body.empty()) return true;
            if (value.expression_body && !value.body.empty())
                for (const auto& item : value.body.front().expressions)
                    if (expands(item)) return true;
        }
        if (!value.accessors.empty()) return true;
        return std::any_of(value.overloads.begin(), value.overloads.end(), [](const auto& overload) { return expands(overload); });
    }

    static bool expands(const Expr& value) {
        if (value.match || (value.type && expands(*value.type))) return true;
        if (value.lambda && expands(*value.lambda)) return true;
        if (std::any_of(value.type_arguments.begin(), value.type_arguments.end(), [](const auto& argument) { return expands(argument); })) return true;
        return std::any_of(value.operands.begin(), value.operands.end(), [](const auto& operand) { return expands(operand); });
    }

    void expression(const Expr& value) {
        if (value.kind == Expr::Kind::Copy && !value.operands.empty()) copy(value);
        // An `html` factory's parameter list, and those of its `or html`
        // signatures, hold types such as `Optional<string>` (Html030.md).
        if (value.html_factory) {
            for (const auto& parameter : value.html_factory->parameters) type(parameter.type);
            for (const auto& overload : value.html_factory->overloads) expression(overload);
        }
        // A braced range, `{0 .. n}`, is an expression, not a block.
        if (value.kind == Expr::Kind::Collection || value.kind == Expr::Kind::Range) {
            const auto first = index_at(value.span.offset);
            if (first < tokens_.size() && tokens_[first].span.offset == value.span.offset && tokens_[first].text == "{") {
                hints_.inline_braces.insert(value.span.offset);
                hints_.tight_braces.insert(value.span.offset);
            }
        }
        if (value.type) type(*value.type);
        types(value.type_arguments);
        if (!value.type_arguments.empty() && !value.operands.empty()) {
            const auto start = value.operands.front().span.offset + value.operands.front().span.length;
            const auto end = value.open ? value.open->offset : value.span.offset + value.span.length;
            if (end > start) angle_span({{}, 1, 1, start, end - start});
        }
        // The operator of a unary expression, whose span may begin at a
        // '(' around it, as in `(-scale) as int`. `not` is a word and keeps
        // the space after it, and the negation a `return ... unless` implies
        // has no token (docs/design/Expressions030.md).
        if ((value.kind == Expr::Kind::Unary && !value.spelled_not && !value.implicit) ||
            value.kind == Expr::Kind::PrefixUpdate) {
            bool found = false;
            in_span(value.span, [&](const Token& token) {
                if (!found && token.text == value.text) {
                    hints_.prefix.insert(token.span.offset);
                    found = true;
                }
            });
            if (!found) hints_.prefix.insert(value.span.offset);
        }
        // `r?` is written without a space (docs/design/Matching030.md).
        if (value.kind == Expr::Kind::Propagate && value.operator_span) hints_.postfix.insert(value.operator_span->offset);
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
        if (value.kind == Expr::Kind::Range && value.step_colon) hints_.spaced_colons.insert(value.step_colon->offset);
        if (value.kind == Expr::Kind::Conditional && value.operands.size() == 3) {
            const auto start = value.operands[1].span.offset + value.operands[1].span.length;
            const auto end = value.operands[2].span.offset;
            if (end > start) {
                in_span({{}, 1, 1, start, end - start}, [&](const Token& token) {
                    if (token.text == ":") hints_.spaced_colons.insert(token.span.offset);
                });
            }
        }
        if (value.lambda) declaration(*value.lambda);
        if (value.match) statement(*value.match);
        for (const auto& operand : value.operands) expression(operand);
    }

    // `new with(source) { a: x }` stays on one line when it is written on one
    // line; written over several, it has one replacement per line and a
    // trailing comma (docs/design/Records030.md, Formatter).
    void copy(const Expr& value) {
        const auto& source = value.operands.front();
        const auto after_source = source.span.offset + source.span.length;
        const Token* open = nullptr;
        const Token* last = nullptr;
        const Token* close = nullptr;
        in_span(value.span, [&](const Token& token) {
            if (token.span.offset < after_source) return;
            if (!open && token.text == "{") open = &token;
            if (token.text == "}") close = &token;
        });
        if (!open || !close) return;
        hints_.comma_lists.insert(open->span.offset);
        for (const auto& token : tokens_) {
            if (token.span.offset >= close->span.offset) break;
            last = &token;
        }
        const auto text = source_.substr(open->span.offset, close->span.offset - open->span.offset);
        if (text.find_first_of("\r\n") == std::string_view::npos) {
            hints_.inline_braces.insert(open->span.offset);
        } else if (last && last != open && last->text != ",") {
            hints_.trailing_commas.insert(close->span.offset);
        }
    }

    std::size_t first_offset(const Declaration& value) const {
        std::size_t result = value.span.offset;
        for (const auto& tag : value.tags) result = std::min(result, tag.span.offset);
        // A tag's span starts at its name: the declaration starts at the
        // '#' before it, or at the '#[' of its group.
        auto index = index_at(result);
        if (index > 0 && index <= tokens_.size() && tokens_[index - 1].text == "[" && index > 1 &&
            tokens_[index - 2].text == "#")
            index -= 2;
        else if (index > 0 && index <= tokens_.size() && tokens_[index - 1].text == "#")
            index -= 1;
        if (index < tokens_.size()) result = std::min(result, tokens_[index].span.offset);
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
        // `derive G<T> { ... }`: its mappings hold no '<' or '>', so every
        // angle in the declaration belongs to the generator's type arguments.
        if (value.derive) angle_span(value.span);
        for (const auto& generic : value.generics) types(generic.bounds);
        for (const auto& parameter : value.parameters) {
            type(parameter.type);
            if (parameter.default_value) expression(*parameter.default_value);
        }

        // Generic declaration delimiters and the enclosing member-list
        // parenthesis are identified from parsed declarations, not capitalization.
        const bool container = value.kind == Declaration::Kind::Class || value.kind == Declaration::Kind::Module ||
                               value.kind == Declaration::Kind::Interface || value.kind == Declaration::Kind::Enum;
        bool head = false;
        bool found_body = false;
        const Token* previous_token = nullptr;
        const bool single_line = source_.substr(value.span.offset, value.span.length).find_first_of("\r\n") ==
                                 std::string_view::npos;
        in_span(value.span, [&](const Token& token) {
            if (!head && (token.text == "class" || token.text == "module" || token.text == "interface" ||
                          token.text == "enum" || token.text == "singleton" || token.text == "func" ||
                          (value.record && token.span.offset == value.record->keyword.offset) ||
                          (value.generator && token.text == "iterator"))) head = true;
            else if (head && !found_body) {
                if (token.text == "<" || token.text == ">") hints_.angles.insert(token.span.offset);
                // `class:Base (`, `module:Base (` and `interface<T>:Base<T> (`.
                if (container && token.text == ":" && previous_token &&
                    (previous_token->kind == TokenKind::Keyword || previous_token->text == ">"))
                    hints_.base_colons.insert(token.span.offset);
                if (token.text == "(") {
                    found_body = true;
                    if (container) hints_.container_opens.insert(token.span.offset);
                    // An enum or a record written on one line stays on one line.
                    const bool multiline_enum = (value.kind != Declaration::Kind::Enum && !value.record) || !single_line;
                    if (container && multiline_enum) hints_.bodies.insert(token.span.offset);
                    if (value.record) hints_.record_bodies.insert(token.span.offset);
                    // A record has one component per line; an enum keeps its
                    // variants on the lines they are written on.
                    if (value.record) hints_.comma_lists.insert(token.span.offset);
                    if (value.kind == Declaration::Kind::Enum) hints_.packed_lists.insert(token.span.offset);
                }
                // A lambda without a parameter list, `func -> T => a < b`,
                // has no generic parameters after `->` or `=>`.
                if (token.text == "{" || token.text == "->" || token.text == "=>" || token.text == ";" || token.text == "=")
                    found_body = true;
            }
            previous_token = &token;
        });
        // A record written over several lines has one component per line and
        // a trailing comma.
        if (value.record && !single_line) {
            const auto close = value.record->close.offset;
            const Token* last = nullptr;
            for (const auto& token : tokens_) {
                if (token.span.offset >= close) break;
                last = &token;
            }
            if (last && last->text != "," && last->text != "(") hints_.trailing_commas.insert(close);
        }

        // Class hook clauses: each on its own line at the members' depth,
        // with one blank line before the first member.
        for (const auto& hook : value.hooks) {
            hints_.before.insert(hook.span.offset);
            if (hook.expression) expression(*hook.expression);
            for (const auto& item : hook.body) statement(item);
        }
        if (!value.hooks.empty() && !value.members.empty()) hints_.blank_before.insert(first_offset(value.members.front()));
        for (std::size_t i = 0; i < value.members.size(); ++i) {
            const auto& member = value.members[i];
            if (member.generated) continue;
            // `or as T = ...` continues the cast declaration before it.
            const auto start = index_at(first_offset(member));
            const bool alternative = start > 0 && start <= tokens_.size() && tokens_[start - 1].text == "or";
            if (alternative) hints_.alternatives.insert(member.span.offset);
            if (i > 0 && !alternative) {
                // A blank line follows a member that spans more than one
                // line as formatted: one written over several lines, or one
                // whose layout expands it.
                const auto& previous = value.members[i - 1];
                const bool multiline = expands(previous) ||
                    source_.substr(previous.span.offset, previous.span.length).find_first_of("\r\n") != std::string_view::npos;
                if (multiline) hints_.blank_before.insert(first_offset(member));
            }
            declaration(member, value.kind == Declaration::Kind::Enum || value.record != nullptr);
        }
        for (const auto& body : value.body) statement(body);
        for (const auto& block : value.startup_blocks) statement(block);
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

    void pattern(const PatternSyntax& value) {
        if (value.kind == PatternSyntax::Kind::Variant && value.named) {
            bool found = false;
            in_span(value.span, [&](const Token& token) {
                if (!found && token.text == "{") {
                    hints_.inline_braces.insert(token.span.offset);
                    found = true;
                }
            });
        }
        for (const auto& element : value.elements) pattern(element);
    }

    void statement(const Statement& value) {
        if (value.kind == Statement::Kind::Yield && !value.expressions.empty())
            hints_.keyword_operands.insert(value.expressions.front().span.offset);
        if (value.kind == Statement::Kind::Defer && !value.expressions.empty())
            hints_.deferred_calls.insert(value.expressions.front().span.offset);
        if (value.kind == Statement::Kind::Using) {
            in_span(value.span, [&, done = false](const Token& token) mutable {
                if (!done && token.text == "(") hints_.control_parens.insert(token.span.offset);
                done = done || token.text == "(";
            });
        }
        if (value.kind == Statement::Kind::Match || value.kind == Statement::Kind::Select) {
            auto from = value.span.offset;
            if (value.kind == Statement::Kind::Match && !value.expressions.empty())
                from = value.expressions.front().span.offset + value.expressions.front().span.length;
            const auto [open, close] = delimited(from, "{", "}");
            if (open && open->span.offset < value.span.offset + value.span.length) hints_.comma_lists.insert(open->span.offset);
        }
        if (value.kind == Statement::Kind::Scope) {
            const auto index = index_at(value.keyword.offset + value.keyword.length);
            if (index < tokens_.size() && tokens_[index].text == "(") hints_.scope_parens.insert(tokens_[index].span.offset);
        }
        if (value.kind == Statement::Kind::Do) {
            const auto [open, close] = delimited(value.span.offset, "{", "}");
            if (close) hints_.do_closes.insert(close->span.offset);
        }
        if (value.kind == Statement::Kind::If && value.keyword.length != 0) hints_.guards.insert(value.keyword.offset);
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
            // A braced variant pattern nested in a general arm stays inline.
            if (arm.general && arm.pattern) pattern(*arm.pattern);
            if (arm.guard) expression(*arm.guard);
            for (const auto& item : arm.body) statement(item);
            if (arm.value) expression(*arm.value);
        }
        for (const auto& arm : value.select_arms) {
            if (arm.operation) expression(*arm.operation);
            for (const auto& item : arm.body) statement(item);
        }
    }
};

// Whether writing `second` directly after `first` would lex as another
// symbol, as `? ?` would as `??` and `? .` as `?.`.
bool would_merge(std::string_view first, std::string_view second) {
    static constexpr std::string_view compounds[] = {
        "===", "<<=", ">>=", "->", "=>", "==", "!=", "<=", ">=", "&&", "||", "?.", "??",
        "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "^=", "|=", "..", "...",
    };
    for (const auto compound : compounds) {
        if (compound.size() <= first.size() || !compound.starts_with(first)) continue;
        if (second.starts_with(compound.substr(first.size()))) return true;
    }
    return false;
}

bool word(std::string_view text) {
    if (text.empty()) return false;
    const auto first = static_cast<unsigned char>(text.front());
    return (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') ||
           (first >= '0' && first <= '9') || first == '_' || first == '"' || first == '\'';
}

// -- Islands (docs/design/Html030.md, "Formatter and editor") ---------------
//
// Inside an HTML or CSS body only leading indentation and trailing whitespace
// change. A line that begins inside text the browser keeps as written (the
// content of <pre> and <textarea>, a quoted attribute value, a comment, a
// string in a hole or in CSS) is left byte for byte, and so is the end of a
// line that ends inside one. Whitespace that contains a line break and
// touches a tag collapses the same way at any width, so re-indenting never
// changes a template's tree.

// Byte ranges of an island body whose line breaks are content.
using Ranges = std::vector<std::pair<std::size_t, std::size_t>>;

// Skip Neura source from `at` to the delimiter that closes it (a `}` of a
// hole or a `)` of a control head), recording strings and comments. Returns
// the offset just past the delimiter, or the end of the text.
std::size_t skip_neura(std::string_view text, std::size_t at, char close, Ranges& kept) {
    int depth = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (const auto end = island::skip_neura_word(text, at); end != at) {
            at = end;
        } else if (c == '"' || c == '\'') {
            const auto start = at++;
            int braces = 0; // holes of an interpolated string
            while (at < text.size() && (text[at] != c || braces > 0)) {
                if (text[at] == '\\') ++at;
                else if (c == '"' && text[at] == '{' && start > 0 && text[start - 1] == '$') ++braces;
                else if (c == '"' && text[at] == '}' && braces > 0) --braces;
                ++at;
            }
            kept.emplace_back(start, std::min(at + 1, text.size()));
            ++at;
        } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
            while (at < text.size() && text[at] != '\n') ++at;
        } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '*') {
            const auto end = text.find("*/", at + 2);
            const auto stop = end == std::string_view::npos ? text.size() : end + 2;
            kept.emplace_back(at, stop);
            at = stop;
        } else if (c == '(' || c == '{' || c == '[') {
            ++depth;
            ++at;
        } else if ((c == ')' || c == '}' || c == ']') && depth > 0) {
            --depth;
            ++at;
        } else if (c == close) {
            return at + 1;
        } else {
            ++at;
        }
    }
    return at;
}

Ranges html_kept(std::string_view text) {
    Ranges kept;
    std::size_t at = 0;
    const auto starts = [&](std::size_t from, std::string_view word) {
        if (text.substr(from, word.size()) != word) return false;
        const auto after = from + word.size();
        return after >= text.size() || !std::isalnum(static_cast<unsigned char>(text[after]));
    };
    while (at < text.size()) {
        const char c = text[at];
        if (text.substr(at, 4) == "<!--") {
            const auto end = text.find("-->", at + 4);
            const auto stop = end == std::string_view::npos ? text.size() : end + 3;
            kept.emplace_back(at, stop);
            at = stop;
        } else if (c == '<' && at + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[at + 1]))) {
            const bool raw = starts(at + 1, "pre") || starts(at + 1, "textarea");
            const auto name = starts(at + 1, "pre") ? std::string_view("</pre") : std::string_view("</textarea");
            ++at;
            while (at < text.size() && text[at] != '>') {
                if (text[at] == '"' || text[at] == '\'') {
                    const auto quote = text[at];
                    const auto end = text.find(quote, at + 1);
                    const auto stop = end == std::string_view::npos ? text.size() : end + 1;
                    kept.emplace_back(at, stop);
                    at = stop;
                } else if (text[at] == '{') {
                    at = skip_neura(text, at + 1, '}', kept);
                } else {
                    ++at;
                }
            }
            if (at < text.size()) ++at;
            if (raw) {
                // The range takes the '<' of the end tag: whitespace before it is content too.
                const auto end = text.find(name, at);
                const auto stop = end == std::string_view::npos ? text.size() : end;
                kept.emplace_back(at, std::min(stop + 1, text.size()));
                at = stop;
            }
        } else if (c == '{') {
            at = skip_neura(text, at + 1, '}', kept);
        } else if (c == '@' && at + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[at + 1]))) {
            const auto open = text.find_first_of("({", at);
            at = open != std::string_view::npos && text[open] == '(' ? skip_neura(text, open + 1, ')', kept) : at + 1;
        } else {
            ++at;
        }
    }
    return kept;
}

Ranges css_kept(std::string_view text) {
    Ranges kept;
    std::size_t at = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '*') {
            const auto end = text.find("*/", at + 2);
            const auto stop = end == std::string_view::npos ? text.size() : end + 2;
            kept.emplace_back(at, stop);
            at = stop;
        } else if (c == '"' || c == '\'') {
            const auto start = at++;
            while (at < text.size() && text[at] != c && text[at] != '\n') {
                if (text[at] == '\\') ++at;
                ++at;
            }
            kept.emplace_back(start, std::min(at + 1, text.size()));
            ++at;
        } else {
            ++at;
        }
    }
    return kept;
}

bool inside(const Ranges& ranges, std::size_t offset) {
    return std::ranges::any_of(ranges, [&](const auto& range) { return range.first < offset && offset < range.second; });
}

// The island token `text` (its braces included), re-indented so that its
// body sits one level inside `depth`.
std::string reindent_island(std::string_view text, TokenKind kind, unsigned depth, FormatOptions options,
                            std::string_view eol) {
    if (text.size() < 2 || text.front() != '{' || text.back() != '}' ||
        text.find_first_of("\r\n") == std::string_view::npos)
        return std::string(text);
    const auto body = text.substr(1, text.size() - 2);
    const auto kept = kind == TokenKind::HtmlIsland ? html_kept(body) : css_kept(body);
    struct Line { std::size_t begin, end; };
    std::vector<Line> lines;
    for (std::size_t at = 0;;) {
        const auto end = body.find_first_of("\r\n", at);
        if (end == std::string_view::npos) {
            lines.push_back({at, body.size()});
            break;
        }
        lines.push_back({at, end});
        at = end + (body[end] == '\r' && end + 1 < body.size() && body[end + 1] == '\n' ? 2 : 1);
    }
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    const auto lead = [&](const Line& line) {
        std::size_t n = line.begin;
        while (n < line.end && blank(body[n])) ++n;
        return n - line.begin;
    };
    // The common indentation of the lines that may move; the first line
    // continues the declaration and the last one holds the closing brace.
    std::size_t common = std::string_view::npos;
    for (std::size_t i = 1; i + 1 < lines.size(); ++i) {
        const auto& line = lines[i];
        if (inside(kept, line.begin) || lead(line) == line.end - line.begin) continue;
        common = std::min(common, lead(line));
    }
    if (common == std::string_view::npos) common = 0;
    const auto unit = options.insert_spaces ? std::string(options.tab_size, ' ') : std::string("\t");
    std::string inner;
    for (unsigned i = 0; i <= depth; ++i) inner += unit;
    std::string outer;
    for (unsigned i = 0; i < depth; ++i) outer += unit;
    std::string result = "{";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        auto content = body.substr(line.begin, line.end - line.begin);
        if (i > 0) result += eol;
        const bool fixed = i > 0 && inside(kept, line.begin);
        const bool open_end = inside(kept, line.end);
        if (!open_end) {
            while (!content.empty() && blank(content.back())) content.remove_suffix(1);
        }
        if (fixed || i == 0) {
            result += content;
            continue;
        }
        const auto indentation = std::min(lead(line), content.size());
        if (i + 1 == lines.size()) {
            // The closing line: the brace sits at the declaration's depth.
            if (indentation == content.size()) {
                result += outer;
                continue;
            }
        }
        if (indentation == content.size()) continue; // a blank line
        result += inner + std::string(content.substr(std::min(common, indentation)));
    }
    return result + "}";
}

// Island bodies that differ at most in the whitespace that begins and ends
// each line, with the same lines.
bool same_island(std::string_view left, std::string_view right) {
    const auto lines = [](std::string_view text) {
        std::vector<std::string_view> result;
        for (std::size_t at = 0;;) {
            const auto end = text.find_first_of("\r\n", at);
            auto line = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
            result.push_back(line);
            if (end == std::string_view::npos) return result;
            at = end + (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n' ? 2 : 1);
        }
    };
    return lines(left) == lines(right);
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
    // Whether the next text starts a line, and the indentation of the
    // line that the last text was written on.
    bool at_line_start() const { return at_start_ || pending_ > 0; }
    unsigned line_indent() const { return line_indent_; }

    void text(std::string_view value, unsigned depth, bool space) {
        flush();
        if (at_start_) {
            line_indent_ = depth;
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
    unsigned line_indent_{};
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
    // A multiline delimiter's items are ','-separated, one per line.
    bool list{};
    // The indentation of the line that holds the opening delimiter: its
    // contents are one level deeper and its closing delimiter is there.
    unsigned outer{};
    // A tag group, `#[A, B]`, which stays on one line.
    bool tags{};
    // Whether the source breaks the line after the opening delimiter: its
    // closing delimiter then keeps a line of its own, as written.
    bool broken{};
    bool packed{};
    bool tight{};
    // The wrapped-line anchor of the statement around a block, restored
    // when the block closes.
    std::optional<unsigned> anchor{};
};

bool keyword_of(const Item& item, std::string_view text) {
    return item.kind == TokenKind::Keyword && item.text == text;
}

std::string render(std::string_view source, const std::vector<Token>& tokens, const Hints& hints, FormatOptions options) {
    auto eol = std::string("\n");
    const auto first_eol = source.find_first_of("\r\n");
    if (first_eol != std::string_view::npos && source[first_eol] == '\r')
        eol = first_eol + 1 < source.size() && source[first_eol + 1] == '\n' ? "\r\n" : "\r";
    const auto line_end = eol;
    Writer out(options, std::move(eol), source.starts_with("\xEF\xBB\xBF"));
    const auto items = with_trivia(source, tokens);

    // The blank line before a member goes before the comments written
    // directly above it, so that they stay attached to it.
    std::set<std::size_t> blank_before;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!hints.blank_before.contains(items[i].offset)) continue;
        auto first = i;
        while (first > 0 && items[first - 1].comment && items[first].lines_before <= 1 &&
               !(first > 1 && !items[first - 2].comment && items[first - 2].line == items[first - 1].line))
            --first;
        blank_before.insert(items[first].offset);
    }

    std::vector<Delimiter> delimiters;
    bool accessor = false;
    // The number of delimiters open when the accessor clause began: a block
    // that the clause opens is already indented under it.
    std::size_t accessor_delimiters = 0;
    std::size_t accessor_end = 0;
    const Item* previous = nullptr;
    // The indentation of the current statement's first line; a wrapped
    // line of the statement is one level deeper.
    unsigned statement_indent = 0;
    // Whether the comments just written interrupt a statement.
    bool comment_continues = false;
    // A wrapped line that begins after `=` or `=>` holds a value of its
    // own, one level deeper than the line before it; one that begins with
    // `or` holds an alternative, one level deeper than the statement. The
    // wrapped lines that follow it are as deep as it is, and a block that it
    // opens is indented under it.
    enum class Anchor { None, Value, Alternative };
    std::optional<unsigned> anchor_level;

    const auto in_inline = [&] { return !delimiters.empty() && !delimiters.back().multiline; };
    const auto base = [&]() -> unsigned {
        for (auto it = delimiters.rbegin(); it != delimiters.rend(); ++it)
            if (it->multiline) return it->outer + 1;
        return 0;
    };
    const auto indent = [&] {
        bool inner = false;
        for (std::size_t k = accessor_delimiters; k < delimiters.size(); ++k) inner |= delimiters[k].multiline;
        return base() + (accessor && !inner ? 1U : 0U);
    };
    // Writes a token or a comment at the start of a line, or after the
    // previous one on the same line.
    // A wrapped line is one level deeper than the line that opened the
    // innermost open delimiter, or than its statement's first line.
    const auto continued = [&]() -> unsigned {
        const unsigned level = anchor_level ? *anchor_level : statement_indent + 1;
        if (!delimiters.empty() && !delimiters.back().multiline) return std::max(level, delimiters.back().outer + 1);
        return level;
    };
    const auto in_tags = [&] {
        return std::any_of(delimiters.begin(), delimiters.end(), [](const Delimiter& delimiter) { return delimiter.tags; });
    };
    // A block opened on a wrapped line of a statement, such as a function
    // body after a wrapped parameter list, is indented under the statement's
    // first line; one opened inside an argument list, under its own line.
    const auto block_outer = [&]() -> unsigned {
        if (!delimiters.empty() && !delimiters.back().multiline) return out.line_indent();
        return anchor_level ? *anchor_level : statement_indent;
    };
    const auto emit = [&](std::string_view text, bool continuation, bool space, Anchor kind = Anchor::None) {
        unsigned level = continuation ? continued() : indent();
        if (out.at_line_start()) {
            if (continuation && kind == Anchor::Value) level = std::max(level, out.line_indent() + 1);
            if (continuation && kind == Anchor::Alternative) level = statement_indent + 1;
            if (!continuation) {
                statement_indent = level;
                anchor_level.reset();
            } else if (kind != Anchor::None) {
                anchor_level = level;
            }
        }
        out.text(text, level, space);
    };
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const auto text = item.text;
        const auto before = previous ? previous->text : std::string_view{};
        const auto next = i + 1 < items.size() ? items[i + 1].text : std::string_view{};
        if (accessor && item.offset >= accessor_end) accessor = false;
        // A line break that the layout does not ask for, written inside a
        // statement, is kept as a wrapped line.
        const bool inside = previous && (previous->comment ? comment_continues : !out.pending());
        if (item.comment) {
            const bool trailing = previous && !previous->comment && item.line == previous->line;
            if (trailing) {
                out.suppress_line();
                comment_continues = inside;
                out.text(text, indent(), true);
            } else {
                if (blank_before.contains(item.offset)) out.line(2);
                else out.line(item.lines_before > 1 ? 2 : 1);
                comment_continues = inside;
                // A comment above an `or` alternative is indented with it.
                std::size_t following = i + 1;
                while (following < items.size() && items[following].comment) ++following;
                const bool before_alternative = inside && following < items.size() && items[following].text == "or" &&
                                                items[following].kind == TokenKind::Keyword;
                if (before_alternative && out.at_line_start()) out.text(text, statement_indent + 1, false);
                else emit(text, inside, false);
            }
            out.line();
            previous = &item;
            continue;
        }
        bool structural = false;
        if (hints.accessors.contains(item.offset)) {
            accessor = true;
            accessor_delimiters = delimiters.size();
            out.line();
            structural = true;
            for (const auto& [end, start] : hints.accessor_ends) {
                if (start == item.offset) { accessor_end = end; break; }
            }
        }
        if (blank_before.contains(item.offset)) { out.line(2); structural = true; }
        else if (hints.before.contains(item.offset)) { out.line(); structural = true; }
        // Tokens that never begin a wrapped line: they join the line before.
        const bool joins = text == "{" || text == ";" || text == "," || text == ")" || text == "]" || text == "}" ||
                           (before == "}" && (text == "else" || text == "or" || text == "while")) ||
                           hints.deferred_calls.contains(item.offset);
        bool continuation = false;
        // A wrapped line that begins after `=` or `=>`, or with `or`, holds a
        // value or an alternative of its own: what it opens and its own
        // wrapped lines are indented under it.
        const Anchor anchor = item.lines_before == 0 ? Anchor::None :
            text == "or" ? Anchor::Alternative : (before == "=" || before == "=>") ? Anchor::Value : Anchor::None;
        // A closing delimiter written on a line of its own, after an opening
        // delimiter that ends its line, stays there, under that line.
        const bool closer = text == ")" || text == "]" || text == "}";
        const bool own_line = closer && item.lines_before > 0 && !delimiters.empty() &&
                              !delimiters.back().multiline && delimiters.back().broken && !in_tags();
        if (own_line) {
            out.line();
        } else if (item.lines_before > 0 && before == "," && !delimiters.empty() && delimiters.back().multiline &&
                   delimiters.back().packed) {
            out.line();
        } else if (item.lines_before > 0 && inside && !structural && !joins && !in_tags()) {
            // A wrapped line; a blank line inside a statement is not kept.
            out.line();
            continuation = true;
        } else if (item.lines_before > 1 && !in_tags() &&
                   (!joins || ((text == "}" || text == ")") && !delimiters.empty() && delimiters.back().multiline))) {
            // A blank line before a block's closing delimiter is kept.
            out.line(2);
        } else if (previous && previous->comment && inside) {
            continuation = true;
        }
        // Whether the next item starts a new source line after this opening
        // delimiter, so that its closing delimiter may keep a line of its own.
        const bool breaks_after = i + 1 < items.size() && (items[i + 1].lines_before > 0 || items[i + 1].comment);

        bool space = previous && !previous->comment;
        if (text == ")" || text == "]" || text == "," || text == ";" || text == "." || text == "?." ||
            (text == ":" && !hints.spaced_colons.contains(item.offset)))
            space = false;
        if (before == "(" || before == "[" || before == "." || before == "?." || before == "#") space = false;
        if (previous && before == "{" && hints.tight_braces.contains(previous->offset)) space = false;
        // A variadic parameter's type follows its `...` directly: `items: ...T`.
        if (before == "...") space = false;
        if (hints.angles.contains(item.offset) || (previous && hints.angles.contains(previous->offset) && before == "<")) space = false;
        if (hints.glued.contains(item.offset) || hints.postfix.contains(item.offset) ||
            (previous && hints.prefix.contains(previous->offset))) space = false;
        if (previous && hints.base_colons.contains(previous->offset)) space = false;
        if (before == ":" && previous) {
            const auto previous_index = i > 1 ? i - 2 : 0;
            const auto colon_owner = i > 1 ? items[previous_index].text : std::string_view{};
            if (colon_owner == "import" || colon_owner == "module" || colon_owner == "class" || colon_owner == "interface") space = false;
        }
        // A '(' or '[' that follows an operand opens its arguments or its
        // index and is written against it: `f(x)`, `values[0]`, `func(`,
        // `new(`. After an operator or a keyword it groups, and is spaced
        // like any operand: `x = (a + b)`, `return (a)`, `in [1, 2]`.
        const bool after_operand = previous && !previous->comment &&
            (before == ")" || before == "]" || before == "}" ||
             (before == ">" && hints.angles.contains(previous->offset)) ||
             previous->kind == TokenKind::Identifier || previous->kind == TokenKind::Integer ||
             previous->kind == TokenKind::Float || previous->kind == TokenKind::String ||
             keyword_of(*previous, "func") || keyword_of(*previous, "new") ||
             keyword_of(*previous, "typeof")) &&
            // `not` is an operator word: `not (a)`, `not [x]` (Expressions030.md).
            before != "not";
        if ((text == "(" || text == "[") && after_operand) space = false;
        if (!space && previous && !previous->comment && previous->kind == TokenKind::Symbol &&
            item.kind == TokenKind::Symbol && would_merge(before, text))
            space = true;
        if (item.kind == TokenKind::HtmlIsland || item.kind == TokenKind::CssIsland) {
            out.text(reindent_island(text, item.kind, indent(), options, line_end), indent(), true);
        } else if (text == "<" && hints.angles.contains(item.offset)) {
            emit(text, continuation, false, anchor);
            delimiters.push_back({text, false, false, false, out.line_indent(), false, breaks_after});
        } else if (text == ">" && hints.angles.contains(item.offset)) {
            if (!delimiters.empty() && delimiters.back().open == "<") delimiters.pop_back();
            emit(text, continuation, false, anchor);
        } else if (text == "(" ) {
            // `match.is (x) {` is written as `match (x) {` is.
            const bool match_is = before == "is" && i >= 3 && items[i - 2].text == "." && items[i - 3].text == "match";
            const bool control = before == "if" || before == "while" || before == "for" || before == "match" ||
                                 hints.scope_parens.contains(item.offset) || match_is ||
                                 hints.control_parens.contains(item.offset);
            const bool body = hints.bodies.contains(item.offset);
            if (control || body || hints.record_bodies.contains(item.offset) || hints.container_opens.contains(item.offset) ||
                before == "module" ||
                before == "class" || before == "interface" || before == "enum" || before == "singleton" ||
                hints.keyword_operands.contains(item.offset) || before == "not" || before == ".." || before == "..=" ||
                before == "!==")
                space = true;
            emit(text, continuation, space, anchor);
            const bool multiline = body && next != ")";
            delimiters.push_back({text, multiline, control, hints.comma_lists.contains(item.offset), block_outer(), false,
                                  breaks_after, hints.packed_lists.contains(item.offset), false, anchor_level});
            if (multiline) out.line();
        } else if (text == "{") {
            const bool inline_brace = hints.inline_braces.contains(item.offset);
            const bool multiline = !inline_brace && next != "}";
            const bool tight = hints.tight_braces.contains(item.offset);
            emit(text, continuation, tight ? space : true, anchor);
            delimiters.push_back({text, multiline, false, hints.comma_lists.contains(item.offset), block_outer(), false,
                                  breaks_after, false, tight, anchor_level});
            if (multiline) out.line();
        } else if (text == "[") {
            emit(text, continuation, space, anchor);
            delimiters.push_back({text, false, false, false, out.line_indent(), before == "#", breaks_after});
        } else if (text == ")" || text == "]" || text == "}") {
            bool multiline = false;
            bool tight = false;
            unsigned outer = 0;
            std::optional<unsigned> saved_anchor;
            if (!delimiters.empty()) {
                multiline = delimiters.back().multiline;
                outer = delimiters.back().outer;
                tight = delimiters.back().tight;
                saved_anchor = delimiters.back().anchor;
                delimiters.pop_back();
            }
            if (multiline && hints.trailing_commas.contains(item.offset)) out.text(",", indent(), false);
            if (multiline) {
                out.line();
                statement_indent = outer;
                anchor_level = saved_anchor;
                out.text(text, outer, false);
            } else if (own_line) {
                out.text(text, outer, false);
            } else {
                emit(text, continuation, text == "}" && before != "{" && !tight);
            }
            const bool do_close = text == "}" && hints.do_closes.contains(item.offset);
            if (text == "}" && next != ";" && next != "," && next != ")" && next != "]" && next != "." &&
                next != "(" && next != "}" && next != "else" && !(next == "while" && do_close) && next != "or" && next != "callback" &&
                next != "=>" && next != "+" && next != "-" && next != "*" && next != "/" && next != "==" && next != "?" && next != ":" &&
                !(i + 1 < items.size() && hints.guards.contains(items[i + 1].offset))) out.line();
        } else {
            // Retain the lexical distinction between a binary operator and
            // a unary operator, and between adjacent operator tokens.
            if (previous && !space && !hints.glued.contains(item.offset) && !hints.angles.contains(item.offset) &&
                !hints.prefix.contains(previous->offset) && !hints.postfix.contains(item.offset) && word(before) && word(text)) space = true;
            emit(text, continuation, space, anchor);
            if (text == ";") {
                if (!in_inline()) out.line();
            } else if (text == ",") {
                if (!delimiters.empty() && delimiters.back().multiline && delimiters.back().list) out.line();
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
#ifdef _WIN32
    // A new file with a unique name replaces the source (retried while an
    // antivirus or indexer holds it), and then gets the source's read-only,
    // hidden, system and not-indexed attributes. A read-only source is made
    // writable for the replacement where the volume cannot replace it
    // otherwise, and keeps its attributes when the replacement fails.
    const auto name = file.wstring();
    const DWORD attributes = ::GetFileAttributesW(name.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("cannot read the attributes of " + file.string());
    const DWORD kept = attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
                                     FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
    if (attributes & FILE_ATTRIBUTE_READONLY) ::SetFileAttributesW(name.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    std::error_code error;
    if (!neura::platform::write_file_atomically(file, formatted, error)) {
        if (attributes & FILE_ATTRIBUTE_READONLY) ::SetFileAttributesW(name.c_str(), attributes);
        throw std::runtime_error("cannot write " + file.string() + ": " + neura::platform::error_message(error));
    }
    if (kept != 0 && !::SetFileAttributesW(name.c_str(), kept | FILE_ATTRIBUTE_ARCHIVE))
        throw std::runtime_error("formatted " + file.string() + " but cannot restore its attributes: " +
                                 neura::platform::error_message(neura::platform::last_error_code()));
#else
    auto temporary = file;
    temporary += ".neura-fmt-XXXXXX";
    auto name = temporary.string();
    std::vector<char> buffer(name.begin(), name.end());
    buffer.push_back('\0');
    const int descriptor = ::mkstemp(buffer.data());
    if (descriptor < 0) throw std::runtime_error("cannot create formatting temporary file beside " + file.string());
    ::close(descriptor);
    temporary = buffer.data();
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
#endif
}

} // namespace

std::string format_source(std::string_view source, const std::string& file, FormatOptions options) {
    if (options.tab_size == 0 || options.tab_size > 16) throw std::invalid_argument("format tab size must be between 1 and 16");
    const auto tokens = lex(source, file);
    const auto syntax = parse(tokens);
    const auto hints = SyntaxHints(tokens, source).collect(syntax);
    auto formatted = render(source, tokens, hints, options);
    const auto result = lex(formatted, file);
    // The only token the formatter adds is the trailing ',' of a multiline
    // record, directly before its ')'.
    if (tokens.size() + hints.trailing_commas.size() != result.size())
        throw std::runtime_error("formatter refused a change to source tokenization");
    for (std::size_t i = 0, j = 0; i < tokens.size(); ++i, ++j) {
        const auto& before = tokens[i];
        if (hints.trailing_commas.contains(before.span.offset) && j + 1 < result.size() && result[j].text == "," &&
            result[j].kind == TokenKind::Symbol)
            ++j;
        const auto& after = result[j];
        const bool island = before.kind == TokenKind::HtmlIsland || before.kind == TokenKind::CssIsland;
        if (before.kind != after.kind || (island ? !same_island(before.text, after.text) : before.text != after.text))
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
    // Validate the whole batch before changing the first file. The source is
    // formatted as the compiler reads it, with CR LF normalized to LF, and
    // written with LF; a file that differs only in its line endings is
    // already formatted (W08).
    for (auto& source : sources)
        source.formatted = format_source(util::normalize_crlf(source.original), source.file.string(), options);
    bool changed = false;
    for (const auto& source : sources) {
        const bool different =
            source.original != source.formatted && !util::only_line_endings_differ(source.original, source.formatted);
        changed |= different;
        if (check && different) std::cerr << source.file.string() << " needs formatting\n";
        else if (inplace && different) write(source.file, source.original, source.formatted);
        else if (!check && !inplace) std::cout << source.formatted;
    }
    return check && changed ? 1 : 0;
}

} // namespace neura::tooling
