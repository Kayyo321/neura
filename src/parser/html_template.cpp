#include "html_template.hpp"
#include "island_positions.hpp"

#include "lexer/island.hpp"

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <iterator>
#include <string_view>
#include <utility>

namespace neura {
namespace {

bool one_of(std::string_view value, std::initializer_list<std::string_view> options) {
    return std::ranges::find(options, value) != options.end();
}

#include "html_tables.inc"

using Entity = std::pair<std::string_view, std::string_view>;
constexpr Entity entities[] = {
#include "html_entities.inc"
};

bool lower(char c) { return c >= 'a' && c <= 'z'; }
bool upper(char c) { return c >= 'A' && c <= 'Z'; }
bool letter(char c) { return lower(c) || upper(c); }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool name_char(char c) { return letter(c) || digit(c) || c == '_' || c == '-'; }
bool word_char(char c) { return name_char(c) || static_cast<unsigned char>(c) >= 0x80; }
bool line_end(char c) { return c == '\n' || c == '\r'; }
bool space(char c) { return c == ' ' || c == '\t' || c == '\f' || line_end(c); }

void append_code_point(std::string& text, unsigned value) {
    if (value < 0x80) {
        text += static_cast<char>(value);
        return;
    }
    const unsigned width = value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
    const auto first = text.size();
    text.resize(first + width);
    for (auto i = first + width - 1; i > first; --i) {
        text[i] = static_cast<char>(0x80 | (value & 0x3f));
        value >>= 6;
    }
    constexpr unsigned leading[] = {0, 0, 0xc0, 0xe0, 0xf0};
    text[first] = static_cast<char>(leading[width] | value);
}

class TemplateParser {
public:
    TemplateParser(std::string_view source, Span origin) : source_(source), positions_(source, std::move(origin)) {}

    TemplateSyntax parse() { return {span(0, source_.size()), nodes(false, false, 0)}; }

private:
    std::string_view source_;
    IslandPositions positions_;
    std::size_t offset_{};
    std::vector<const TemplateElement*> ancestors_;

    bool done() const { return offset_ == source_.size(); }

    char peek(std::size_t ahead = 0) const {
        return offset_ + ahead < source_.size() ? source_[offset_ + ahead] : '\0';
    }

    bool starts(std::string_view text) const { return source_.substr(offset_).starts_with(text); }

    bool take(char value) {
        if (done() || peek() != value) return false;
        ++offset_;
        return true;
    }

    void whitespace() {
        while (!done() && space(peek())) ++offset_;
    }

    Span span(std::size_t begin, std::size_t end) const { return positions_.span(begin, end); }

    Span here() const { return span(offset_, offset_ + !done()); }

    bool control_here() const {
        return peek() == '@' && letter(peek(1)) && (offset_ == 0 || !word_char(source_[offset_ - 1]));
    }

    std::string_view name(bool attribute = false) {
        const auto start = offset_;
        while (name_char(peek()) || (attribute && (peek() == ':' || peek() == '.'))) ++offset_;
        return source_.substr(start, offset_ - start);
    }

    void check_depth(std::size_t depth) const {
        constexpr std::size_t maximum = 255;
        if (depth >= maximum)
            throw Diagnostic(span(0, source_.size()), "template exceeds nesting limit U-2").with_code("E1015")
                .label("nesting depth " + std::to_string(depth + 1) + " exceeds " + std::to_string(maximum));
    }

    std::vector<Token> neura_tokens(std::size_t begin, std::size_t end) const {
        std::vector<Token> tokens;
        try {
            tokens = lex_template_neura(source_.substr(begin, end - begin), span(begin, begin).file);
        } catch (const Diagnostic& diagnostic) {
            auto error = diagnostic;
            const auto relocated = begin + diagnostic.span.offset;
            error.span = span(relocated, relocated + error.span.length);
            throw error;
        }
        for (auto& token : tokens) {
            const auto relocated = begin + token.span.offset;
            token.span = span(relocated, relocated + token.span.length);
        }
        return tokens;
    }

    TemplateNode hole() {
        const auto start = offset_;
        const auto closing = island::close_neura(source_, start, '}');
        if (!closing)
            throw Diagnostic(span(start, source_.size()), "malformed template hole").with_code("E0247")
                .label("expected a closing '}'");
        offset_ = *closing + 1;
        try {
            auto tokens = neura_tokens(start + 1, *closing);
            return {span(start, offset_), TemplateHole{parse_template_expression(tokens)}};
        } catch (const Diagnostic& error) {
            throw Diagnostic(span(start, offset_), "malformed template hole").with_code("E0247")
                .label("write one Neura expression inside the braces").label(error.span, error.what());
        }
    }

    [[noreturn]] void bad_reference(std::size_t start) const {
        throw Diagnostic(span(start, offset_), "malformed or unknown character reference").with_code("E0246")
            .label("write a known reference with its ';', or use '&amp;' for a literal ampersand");
    }

    void reference(std::string& result) {
        const auto start = offset_++;
        const bool numeric = take('#');
        const bool hex = numeric && take('x');
        const auto value_start = offset_;
        while (letter(peek()) || digit(peek())) ++offset_;
        const auto value = source_.substr(value_start, offset_ - value_start);
        if (!take(';') || value.empty()) bad_reference(start);
        if (numeric) {
            unsigned scalar = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), scalar, hex ? 16 : 10);
            if (error != std::errc{} || end != value.data() + value.size() || scalar == 0 || scalar > 0x10ffff ||
                (scalar >= 0xd800 && scalar <= 0xdfff)) bad_reference(start);
            // HTML interprets the C1 numeric references with this replacement table.
            constexpr unsigned c1[] = {
                0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021,
                0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d, 0x8f,
                0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178,
            };
            if (scalar >= 0x80 && scalar < 0xa0) scalar = c1[scalar - 0x80];
            append_code_point(result, scalar);
        } else {
            const auto entity = std::ranges::lower_bound(entities, value, {}, &Entity::first);
            if (entity == std::end(entities) || entity->first != value) bad_reference(start);
            result += entity->second;
        }
    }

    std::string quoted() {
        const auto quote = peek();
        const auto start = offset_++;
        std::string result;
        while (!done() && peek() != quote) {
            if (peek() == '&') reference(result);
            else result += source_[offset_++];
        }
        if (!take(quote))
            throw Diagnostic(span(start, offset_), "malformed attribute").with_code("E0245")
                .label("close this quoted value");
        return result;
    }

    bool text_here() const {
        return !done() && peek() != '<' && peek() != '{' && peek() != '}' && !control_here();
    }

    std::string text(bool preserve, bool left_boundary) {
        std::string result;
        while (text_here()) {
            if (peek() == '&') {
                reference(result);
            } else if (preserve || !space(peek())) {
                result += source_[offset_++];
            } else {
                const auto start = offset_;
                whitespace();
                const bool multiline = source_.substr(start, offset_ - start).find_first_of("\r\n") != std::string_view::npos;
                const bool right_boundary = done() || (peek() == '<' && !starts("<!--")) ||
                                            peek() == '}' || control_here();
                if (!multiline || (!(left_boundary && result.empty()) && !right_boundary)) result += ' ';
            }
        }
        return result;
    }

    void comment() {
        const auto start = offset_;
        const auto close = source_.find("--", offset_ + 4);
        if (close == std::string_view::npos || !source_.substr(close).starts_with("-->"))
            throw Diagnostic(span(start, close == std::string_view::npos ? source_.size() : close + 2),
                             "malformed template comment").with_code("E0244")
                .label("close the comment with '-->' and keep '--' out of its text");
        offset_ = close + 3;
    }

    [[noreturn]] void bad_tag(std::size_t start, std::string reason) const {
        throw Diagnostic(span(start, offset_ + !done()), "malformed template tag").with_code("E0244")
            .label(std::move(reason));
    }

    [[noreturn]] void forbidden(const Span& location, std::string reason) const {
        throw Diagnostic(location, "not allowed in a template").with_code("E0251").label(std::move(reason));
    }

    [[noreturn]] void bad_directive(const Span& location, std::string reason) const {
        throw Diagnostic(location, "malformed binding directive").with_code("E0250").label(std::move(reason));
    }

    TemplateAttribute attribute(bool svg) {
        const auto start = offset_;
        if (peek() == '{') {
            const auto end = island::close_neura(source_, offset_, '}').value_or(source_.size() - 1);
            forbidden(span(start, end + 1), "write each attribute explicitly");
        }
        auto spelling = name(true);
        const auto name_span = span(start, offset_);
        if (spelling.empty() || !letter(spelling.front()))
            throw Diagnostic(here(), "malformed attribute").with_code("E0245").label("expected an attribute name");
        TemplateAttribute result{TemplateAttribute::Kind::Attribute, std::string(spelling), name_span, {}, {}, {}};
        const auto colon = spelling.find(':');
        if (colon != std::string_view::npos) {
            const auto prefix = spelling.substr(0, colon);
            auto target = spelling.substr(colon + 1);
            if (prefix == "prop") result.kind = TemplateAttribute::Kind::Property;
            else if (prefix == "var") result.kind = TemplateAttribute::Kind::Variable;
            else if (prefix == "on") result.kind = TemplateAttribute::Kind::Event;
            else bad_directive(name_span, "use one of 'on:', 'prop:', or 'var:'");
            if (result.kind == TemplateAttribute::Kind::Event) {
                const auto first_dot = target.find('.');
                auto modifier_start = start + colon + 1 + (first_dot == std::string_view::npos ? target.size() : first_dot);
                target = target.substr(0, first_dot);
                while (modifier_start < start + spelling.size()) {
                    const auto begin = ++modifier_start;
                    while (modifier_start < start + spelling.size() && source_[modifier_start] != '.') ++modifier_start;
                    const auto modifier = source_.substr(begin, modifier_start - begin);
                    if (modifier.empty() || !std::ranges::all_of(modifier, name_char))
                        bad_directive(name_span, "write a name after every event modifier dot");
                    result.modifiers.push_back({TokenKind::Identifier, std::string(modifier), span(begin, modifier_start)});
                }
            }
            if (target.empty() || !std::ranges::all_of(target, name_char) ||
                (result.kind == TemplateAttribute::Kind::Variable &&
                 (!target.starts_with("--") || target.size() == 2)))
                bad_directive(name_span, "write a directive name after the ':'");
            result.name = target;
        } else {
            if (spelling.starts_with("on"))
                forbidden(name_span, "use 'on:event={handler}' for event handlers");
            if (spelling == "style")
                forbidden(name_span, "use a stylesheet class or 'var:--name={value}'");
            if (std::ranges::any_of(spelling, upper) && !(svg && spelling == "viewBox"))
                throw Diagnostic(name_span, "malformed attribute").with_code("E0245")
                    .label("write attribute names in lowercase");
        }
        const auto next = offset_;
        whitespace();
        const bool has_value = take('=');
        if (has_value) whitespace();
        else offset_ = next;
        if (has_value && peek() == '{') {
            auto value = hole();
            result.value_span = value.span;
            result.value = std::move(std::get<TemplateHole>(value.value).expression);
        } else if (result.kind != TemplateAttribute::Kind::Attribute) {
            bad_directive(name_span, "a directive takes a whole expression such as 'on:click={handler}'");
        } else if (has_value) {
            if (peek() != '\'' && peek() != '"')
                throw Diagnostic(span(start, offset_ + !done()), "malformed attribute").with_code("E0245")
                    .label("quote the value or enclose an expression in braces");
            const auto begin = offset_;
            result.value = quoted();
            result.value_span = span(begin, offset_);
        }
        return result;
    }

    [[noreturn]] void bad_nesting(Span location, const TemplateElement* parent, std::string reason) const {
        auto error = Diagnostic(std::move(location), "template nesting a browser would repair").with_code("E0243")
            .label(std::move(reason));
        if (parent) error.label(parent->opening, "enclosing element starts here");
        throw error;
    }

    [[noreturn]] void missing_end(const TemplateElement& element, const Span& found) const {
        throw Diagnostic(element.opening, "mismatched or unclosed template element").with_code("E0242")
            .label("close '<" + element.name + ">' with '</" + element.name + ">'")
            .label(found, "the template reached this point first");
    }

    void validate_nesting(const TemplateElement& element) const {
        if (element.svg) return;
        const auto* parent = ancestors_.empty() ? nullptr : ancestors_.back();
        const std::string_view parent_name = parent ? parent->name : std::string_view{};
        for (const auto* ancestor : ancestors_) {
            if (element.name == ancestor->name && one_of(element.name, {"li", "option", "optgroup", "dt", "dd"}))
                missing_end(*ancestor, element.opening);
            if (ancestor->name == "p" && closes_paragraph(element.name))
                bad_nesting(element.opening, ancestor, "this element would close the enclosing '<p>'");
            if (one_of(ancestor->name, {"a", "button"}) && interactive(element))
                bad_nesting(element.opening, ancestor, "move this interactive element outside the enclosing tag");
            if (ancestor->name == "form" && element.name == "form")
                bad_nesting(element.opening, ancestor, "a form cannot contain another form");
        }
        // A top-level element's parent is wherever the caller places the view,
        // so only an enclosing element of this template constrains it.
        const auto required = parent ? required_parent(element.name, parent_name) : std::string_view{};
        if (!required.empty()) bad_nesting(element.opening, parent, "use " + std::string(required) + " as the parent");
        if (parent && !parent_accepts(parent_name, element.name))
            bad_nesting(element.opening, parent, "this child changes how the browser builds the parent");
    }

    std::pair<std::string_view, Span> end_tag() {
        const auto start = offset_;
        offset_ += 2;
        const auto spelling = name();
        whitespace();
        if (spelling.empty() || !take('>')) bad_tag(start, "write a closing tag in the form '</name>'");
        const auto location = span(start, offset_);
        if (island::html_void_element(spelling))
            throw Diagnostic(location, "malformed template tag").with_code("E0244")
                .label("void elements have no closing tag");
        return {spelling, location};
    }

    TemplateNode element(bool preserve, std::size_t depth) {
        check_depth(depth);
        const auto start = offset_++;
        if (peek() == '!' || peek() == '?' || peek() == '{') {
            const auto end = source_.find('>', offset_);
            forbidden(span(start, end == std::string_view::npos ? offset_ + 1 : end + 1),
                      "use a static element name or an HTML comment");
        }
        const auto name_start = offset_;
        const auto spelling = name();
        if (spelling.empty() || !letter(spelling.front())) bad_tag(start, "expected an element name after '<'");
        const auto name_span = span(name_start, offset_);
        const bool svg = spelling == "svg" || (!ancestors_.empty() && ancestors_.back()->svg);
        if (std::ranges::any_of(spelling, upper) && !(svg && svg_cased_name(spelling)))
            bad_tag(start, "write the element name in lowercase");
        if (one_of(spelling, {"script", "style"}))
            forbidden(span(start, offset_), spelling == "style" ? "declare a separate CSS stylesheet" : "bind actions with event handlers");
        auto result = std::make_shared<TemplateElement>();
        result->name = spelling;
        result->name_span = name_span;
        result->svg = svg;
        bool self_closing = false;
        while (!done()) {
            const auto last = offset_;
            whitespace();
            if (done()) break;
            if (starts("/>")) {
                offset_ += 2;
                self_closing = true;
                break;
            }
            if (take('>')) break;
            if (offset_ == last) bad_tag(start, "separate attributes from the tag name and each other with whitespace");
            auto item = attribute(svg);
            const auto same = std::ranges::find_if(result->attributes, [&](const TemplateAttribute& prior) {
                const bool property = item.kind == TemplateAttribute::Kind::Property ||
                                      prior.kind == TemplateAttribute::Kind::Property;
                const bool attribute_property = property &&
                    !one_of_directive(item.kind) && !one_of_directive(prior.kind);
                return prior.name == item.name && (prior.kind == item.kind || attribute_property);
            });
            if (same != result->attributes.end())
                throw Diagnostic(item.name_span, "malformed or repeated attribute").with_code("E0245")
                    .label("this attribute or binding is written more than once")
                    .label(same->name_span, "first written here");
            result->attributes.push_back(std::move(item));
        }
        if (offset_ == start || source_[offset_ - 1] != '>') bad_tag(start, "expected '>' to close the start tag");
        result->opening = span(start, offset_);
        if (self_closing && !svg && !island::html_void_element(spelling))
            throw Diagnostic(result->opening, "malformed template tag").with_code("E0244")
                .label("write both a start tag and an end tag");
        validate_nesting(*result);
        if (!(self_closing || (!svg && island::html_void_element(spelling)))) {
            ancestors_.push_back(result.get());
            result->children = nodes(false, preserve || one_of(spelling, {"pre", "textarea"}), depth + 1);
            ancestors_.pop_back();
            if (!starts("</")) missing_end(*result, here());
            const auto [closing, location] = end_tag();
            if (closing != spelling) missing_end(*result, location);
        }
        return {span(start, offset_), std::move(result)};
    }

    static bool one_of_directive(TemplateAttribute::Kind kind) {
        return kind == TemplateAttribute::Kind::Event || kind == TemplateAttribute::Kind::Variable;
    }

    [[noreturn]] void bad_control(std::size_t start, std::string reason) const {
        throw Diagnostic(span(start, offset_ + !done()), "malformed template control block").with_code("E0249")
            .label(std::move(reason));
    }

    [[noreturn]] void bad_control_head(std::size_t start, const Diagnostic& cause, std::string reason) const {
        auto extent = span(start, offset_);
        extent.length = cause.span.offset + cause.span.length - extent.offset;
        throw Diagnostic(extent, "malformed template control block").with_code("E0249")
            .label(std::move(reason)).label(cause.span, cause.what());
    }

    std::vector<Token> control_header(std::size_t start) {
        whitespace();
        if (peek() != '(') bad_control(start, "enclose the control head in parentheses");
        const auto closing = island::close_neura(source_, offset_, ')');
        if (!closing) bad_control(start, "close the control head with ')'");
        const auto begin = offset_ + 1;
        offset_ = *closing + 1;
        return neura_tokens(begin, *closing);
    }

    std::vector<TemplateNode> control_body(std::size_t start, bool preserve, std::size_t depth) {
        whitespace();
        if (!take('{')) bad_control(start, "enclose the control body in braces");
        auto body = nodes(true, preserve, depth + 1);
        if (!take('}')) bad_control(start, "close the control body with '}'");
        return body;
    }

    TemplateNode control(bool preserve, std::size_t depth) {
        check_depth(depth);
        const auto start = offset_++;
        const auto word = name();
        if (word == "for") {
            auto head = [&]() {
                try {
                    return parse_template_for_head(control_header(start));
                } catch (const Diagnostic& error) {
                    if (error.code() == "E0249") throw;
                    bad_control_head(start, error, "write '@for (let item in items; key: expression)'");
                }
            }();
            auto children = control_body(start, preserve, depth);
            return {span(start, offset_), std::make_shared<TemplateFor>(TemplateFor{std::move(head), std::move(children)})};
        }
        if (word != "if") bad_control(start, word == "else" ? "'@else' must follow an '@if' branch" : "use '@if' or '@for'");

        auto result = std::make_shared<TemplateIf>();
        auto branch = start;
        bool conditional = true;
        while (true) {
            std::optional<Expr> condition;
            if (conditional) {
                try {
                    condition = parse_template_expression(control_header(branch));
                } catch (const Diagnostic& error) {
                    if (error.code() == "E0249") throw;
                    bad_control_head(branch, error, "write one Neura expression in the condition");
                }
            }
            auto children = control_body(branch, preserve, depth);
            result->branches.push_back({span(branch, offset_), std::move(condition), std::move(children)});
            if (!conditional) break;
            const auto body_end = offset_;
            whitespace();
            if (!starts("@else") || name_char(peek(5))) {
                offset_ = body_end;
                break;
            }
            branch = offset_;
            offset_ += 5;
            whitespace();
            conditional = starts("if") && !name_char(peek(2));
            if (conditional) offset_ += 2;
        }
        return {span(start, offset_), std::move(result)};
    }

    std::vector<TemplateNode> nodes(bool in_control, bool preserve, std::size_t depth) {
        std::vector<TemplateNode> result;
        bool left_boundary = true;
        while (!done()) {
            const auto start = offset_;
            if (starts("</")) {
                if (!ancestors_.empty() || in_control) break;
                const auto [name, location] = end_tag();
                throw Diagnostic(location, "mismatched or unclosed template element").with_code("E0242")
                    .label("no start tag matches '</" + std::string(name) + ">'");
            }
            if (starts("<!--")) {
                comment();
            } else if (peek() == '<') {
                result.push_back(element(preserve, depth));
                left_boundary = true;
            } else if (peek() == '{') {
                if (starts("{...")) {
                    const auto end = island::close_neura(source_, offset_, '}').value_or(source_.size() - 1);
                    forbidden(span(start, end + 1), "write every attribute explicitly");
                }
                result.push_back(hole());
                left_boundary = false;
            } else if (peek() == '}') {
                if (in_control) break;
                throw Diagnostic(here(), "stray brace in template text").with_code("E0248")
                    .label("write '&#125;' for a literal '}'");
            } else if (control_here()) {
                result.push_back(control(preserve, depth));
                left_boundary = true;
            } else {
                auto value = text(preserve, left_boundary);
                if (!value.empty()) {
                    const auto* parent = ancestors_.empty() ? nullptr : ancestors_.back();
                    if (parent && table_only_elements(parent->name) && !std::ranges::all_of(value, space))
                        bad_nesting(span(start, offset_), parent, "move text outside the table structure");
                    result.push_back({span(start, offset_), TemplateText{std::move(value)}});
                    left_boundary = false;
                }
            }
        }
        return result;
    }
};

} // namespace

TemplateSyntax parse_template(std::string_view body, Span origin) {
    return TemplateParser(body, std::move(origin)).parse();
}

std::vector<std::pair<std::size_t, std::size_t>> srcset_urls(std::string_view value) {
    std::vector<std::pair<std::size_t, std::size_t>> urls;
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; };
    std::size_t at = 0;
    while (at < value.size()) {
        while (at < value.size() && (blank(value[at]) || value[at] == ',')) ++at;
        if (at == value.size()) break;
        const auto begin = at;
        while (at < value.size() && !blank(value[at])) ++at;
        auto end = at;
        bool separated = false;
        while (end > begin && value[end - 1] == ',') {
            --end;
            separated = true;
        }
        urls.emplace_back(begin, end);
        if (separated) continue;
        // Descriptors run to the next comma outside parentheses.
        int depth = 0;
        while (at < value.size() && (value[at] != ',' || depth > 0)) {
            if (value[at] == '(') ++depth;
            else if (value[at] == ')' && depth > 0) --depth;
            ++at;
        }
    }
    return urls;
}

} // namespace neura
