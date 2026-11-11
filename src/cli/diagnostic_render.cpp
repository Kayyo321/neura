#include "diagnostic_render.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

namespace neura::cli {
namespace {

struct Range {
    char32_t first;
    char32_t last;
};

// Combining marks and other code points that take no column of their own.
constexpr Range zero_width[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF}, {0x05C1, 0x05C2},
    {0x05C4, 0x05C5}, {0x05C7, 0x05C7}, {0x0610, 0x061A}, {0x064B, 0x065F}, {0x0670, 0x0670},
    {0x06D6, 0x06DC}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8}, {0x06EA, 0x06ED}, {0x0711, 0x0711},
    {0x0730, 0x074A}, {0x07A6, 0x07B0}, {0x07EB, 0x07F3}, {0x0816, 0x082D}, {0x0859, 0x085B},
    {0x08D3, 0x0902}, {0x093A, 0x093A}, {0x093C, 0x093C}, {0x0941, 0x0948}, {0x094D, 0x094D},
    {0x0951, 0x0957}, {0x0962, 0x0963}, {0x0981, 0x0981}, {0x09BC, 0x09BC}, {0x09C1, 0x09C4},
    {0x09CD, 0x09CD}, {0x0A01, 0x0A02}, {0x0A3C, 0x0A3C}, {0x0A41, 0x0A51}, {0x0E31, 0x0E31},
    {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x0EB1, 0x0EB1}, {0x0EB4, 0x0EBC}, {0x0EC8, 0x0ECD},
    {0x1160, 0x11FF}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x202A, 0x202E},
    {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0x302A, 0x302D}, {0x3099, 0x309A}, {0xFE00, 0xFE0F},
    {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0x1F3FB, 0x1F3FF}, {0xE0001, 0xE007F}, {0xE0100, 0xE01EF},
};

// East Asian Wide and Fullwidth code points, and emoji presented as wide.
constexpr Range wide[] = {
    {0x1100, 0x115F}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0},
    {0x23F3, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F},
    {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE}, {0x26C4, 0x26C5},
    {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F3}, {0x26F5, 0x26F5},
    {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728},
    {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
    {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55},
    {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xA000, 0xA4CF},
    {0xA960, 0xA97F}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE6F},
    {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x16FE0, 0x16FE4}, {0x17000, 0x18AFF}, {0x1B000, 0x1B2FF},
    {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F251},
    {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF}, {0x1FA70, 0x1FAFF}, {0x20000, 0x2FFFD},
    {0x30000, 0x3FFFD},
};

template <std::size_t N>
bool contains(const Range (&table)[N], char32_t c) {
    const auto* found = std::upper_bound(std::begin(table), std::end(table), c,
                                         [](char32_t value, const Range& range) { return value < range.first; });
    return found != std::begin(table) && c <= (found - 1)->last;
}

// Decodes the code point at `text[at]` into `c` and returns its length in
// bytes. Invalid UTF-8 decodes one byte as U+FFFD.
std::size_t decode(std::string_view text, std::size_t at, char32_t& c) {
    const auto lead = static_cast<unsigned char>(text[at]);
    c = 0xFFFD;
    if (lead < 0x80) {
        c = lead;
        return 1;
    }
    std::size_t length = lead >= 0xF0 && lead <= 0xF4 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC2 && lead < 0xE0 ? 2 : 0;
    if (length == 0 || at + length > text.size()) return 1;
    char32_t value = lead & (length == 2 ? 0x1F : length == 3 ? 0x0F : 0x07);
    for (std::size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[at + i]);
        if ((next & 0xC0) != 0x80) return 1;
        value = (value << 6) | (next & 0x3F);
    }
    const char32_t minimum = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return 1;
    c = value;
    return length;
}

// One code point of a source line as it is displayed.
struct Cell {
    std::size_t begin;
    std::size_t end;
    std::string text;
    std::size_t width;
};

// Tabs become four spaces, and control characters and invalid UTF-8 become
// U+FFFD, so that a snippet cannot move the cursor.
std::vector<Cell> cells(std::string_view line) {
    std::vector<Cell> result;
    for (std::size_t at = 0; at < line.size();) {
        char32_t c;
        const auto length = decode(line, at, c);
        Cell cell{at, at + length, std::string(line.substr(at, length)), 1};
        if (c == '\t') {
            cell.text = "    ";
            cell.width = 4;
        } else if (c < 0x20 || c == 0x7F || (c == 0xFFFD && length == 1 && line[at] != '\xEF')) {
            cell.text = "\xEF\xBF\xBD";
        } else {
            cell.width = display_width(c);
        }
        result.push_back(std::move(cell));
        at += length;
    }
    return result;
}

std::size_t total_width(const std::vector<Cell>& line) {
    std::size_t width = 0;
    for (const auto& cell : line) width += cell.width;
    return width;
}

// The display column of byte `offset` of the line. An offset inside a code
// point is that code point's column.
std::size_t column_of(const std::vector<Cell>& line, std::size_t offset) {
    std::size_t column = 0;
    for (const auto& cell : line) {
        if (offset < cell.end) break;
        column += cell.width;
    }
    return column;
}

// A window of a line that fits the terminal: columns [left, right) with
// "..." before and after when text is cut off.
struct View {
    std::string text;
    std::size_t left = 0;
    std::size_t right = 0;
    bool prefix = false;

    // The display column in the view of line column `column`.
    std::size_t map(std::size_t column) const {
        const auto clipped = std::clamp(column, left, right);
        return clipped - left + (prefix ? 3 : 0);
    }
};

// Shows the line from about ten columns before column `first`, the leftmost
// label, as far as `available` columns allow, with "..." where text is cut.
View view(const std::vector<Cell>& line, std::size_t first, std::size_t available) {
    const auto total = total_width(line);
    View result;
    result.right = total;
    if (total > available) {
        constexpr std::size_t context = 10;
        result.left = first > context ? first - context : 0;
        result.prefix = result.left > 0;
        const auto budget = available - (result.prefix ? 3 : 0);
        if (result.left + budget >= total) {
            // The window reaches the end: fill it from the right instead.
            result.left = total - (available - 3);
            result.prefix = true;
        } else {
            result.right = result.left + budget - 3;
        }
    }
    std::size_t column = 0;
    if (result.prefix) result.text = "...";
    for (const auto& cell : line) {
        if (column >= result.left && column + cell.width <= result.right) result.text += cell.text;
        column += cell.width;
    }
    if (result.right < total) result.text += "...";
    return result;
}

class Paint {
public:
    explicit Paint(bool enabled) : enabled_(enabled) {}
    std::string operator()(std::string_view text, std::string_view style) const {
        if (!enabled_ || text.empty()) return std::string(text);
        return "\x1b[" + std::string(style) + "m" + std::string(text) + "\x1b[0m";
    }

private:
    bool enabled_;
};

constexpr std::string_view blue = "1;34";
constexpr std::string_view bold = "1";

std::string_view severity_word(Severity severity) {
    switch (severity) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Note: return "note";
    case Severity::Help: return "help";
    }
    return "error";
}

std::string_view severity_style(Severity severity) {
    switch (severity) {
    case Severity::Error: return "1;31";
    case Severity::Warning: return "1;33";
    case Severity::Note: return "1;32";
    case Severity::Help: return "1;36";
    }
    return "1;31";
}

std::string location(const Span& span) {
    return span.file + ":" + std::to_string(span.line) + ":" + std::to_string(span.column);
}

// A label placed on its first line.
struct Mark {
    std::size_t line;
    std::size_t start;   // byte in the line
    std::size_t end;     // byte in the line
    bool continues;      // the span runs past the end of this line
    bool primary;
    std::string text;
};

struct Section {
    std::string name;
    std::shared_ptr<const SourceFile> file;
    Span first;
    std::vector<Mark> marks;
};

std::optional<Mark> place(const SourceFile& file, const DiagnosticLabel& label) {
    const auto& text = file.text();
    if (label.span.offset > text.size()) return std::nullopt;
    const auto line = file.line_of(label.span.offset);
    const auto start = file.line_start(line);
    const auto line_length = file.line_text(line).size();
    Mark mark{line, label.span.offset - start, 0, false, label.primary, label.text};
    mark.start = std::min(mark.start, line_length);
    const auto end = std::min(label.span.offset + label.span.length, text.size());
    if (label.span.length > 0 && file.line_of(end - 1) > line) {
        mark.continues = true;
        mark.end = line_length;
    } else {
        mark.end = std::min(end - start, line_length);
    }
    return mark;
}

std::size_t digits(std::size_t value) {
    std::size_t count = 1;
    while (value >= 10) {
        value /= 10;
        ++count;
    }
    return count;
}

struct SuggestionLine {
    std::shared_ptr<const SourceFile> file;
    std::size_t line;
    std::string text;
    // Display columns [first, last) and the character under them.
    std::vector<std::tuple<std::size_t, std::size_t, char>> marks;
};

// The edited lines that show a suggestion, or nothing when an edit is outside
// the known sources or spans lines.
std::optional<std::vector<SuggestionLine>> suggestion_lines(const DiagnosticSuggestion& suggestion,
                                                            const SourceMap& sources) {
    if (suggestion.edits.empty()) return std::nullopt;
    std::map<std::pair<std::string, std::size_t>, std::vector<const DiagnosticEdit*>> groups;
    std::map<std::string, std::shared_ptr<const SourceFile>> files;
    for (const auto& edit : suggestion.edits) {
        auto file = sources.find(edit.span.file);
        if (!file || edit.span.offset + edit.span.length > file->text().size()) return std::nullopt;
        const auto line = file->line_of(edit.span.offset);
        const auto line_end = file->line_start(line) + file->line_text(line).size();
        if (edit.span.offset + edit.span.length > line_end || edit.replacement.find_first_of("\r\n") != std::string::npos)
            return std::nullopt;
        files.emplace(edit.span.file, file);
        groups[{edit.span.file, line}].push_back(&edit);
    }
    std::vector<SuggestionLine> result;
    for (auto& [key, edits] : groups) {
        const auto& file = files.at(key.first);
        const auto original = file->line_text(key.second);
        const auto base = file->line_start(key.second);
        std::ranges::sort(edits, {}, [](const DiagnosticEdit* edit) { return edit->span.offset; });
        SuggestionLine shown{file, key.second, {}, {}};
        const bool removals = std::ranges::all_of(edits, [](const DiagnosticEdit* edit) { return edit->replacement.empty(); });
        if (removals) {
            const auto line = cells(original);
            shown.text = std::string(original);
            for (const auto* edit : edits) {
                const auto start = edit->span.offset - base;
                shown.marks.emplace_back(column_of(line, start), column_of(line, start + edit->span.length), '-');
            }
            result.push_back(std::move(shown));
            continue;
        }
        std::size_t at = 0;
        std::vector<std::tuple<std::size_t, std::size_t, char>> bytes;
        for (const auto* edit : edits) {
            const auto start = edit->span.offset - base;
            if (start < at) continue;
            shown.text.append(original.substr(at, start - at));
            const auto begin = shown.text.size();
            shown.text += edit->replacement;
            if (!edit->replacement.empty())
                bytes.emplace_back(begin, shown.text.size(), edit->span.length == 0 ? '+' : '~');
            at = start + edit->span.length;
        }
        shown.text.append(original.substr(at));
        const auto line = cells(shown.text);
        for (const auto& [begin, end, c] : bytes) shown.marks.emplace_back(column_of(line, begin), column_of(line, end), c);
        result.push_back(std::move(shown));
    }
    return result;
}

// Appends `lead` and `text`, indenting the continuation lines of `text` by
// `indent` columns.
void append_lines(std::string& out, const std::string& lead, std::size_t indent, std::string_view text) {
    bool first = true;
    std::size_t start = 0;
    for (;;) {
        const auto end = text.find('\n', start);
        const auto piece = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!first) out += '\n';
        out += first ? lead : std::string(indent, ' ');
        out += piece;
        first = false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    out += '\n';
}

std::vector<std::string> all_notes(const Diagnostic& diagnostic, const SourceMap& sources) {
    std::vector<std::string> notes;
    if (const auto* data = diagnostic.data()) notes = data->notes;
    if (const auto file = sources.find(diagnostic.span.file);
        file && file->kind() == SourceKind::Generated && !file->origin().empty())
        notes.push_back("this module was generated from the C header '" + file->origin() + "'");
    return notes;
}

std::string rtrim(std::string text) {
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

void json_string(std::string& out, std::string_view text) {
    out += '"';
    static constexpr char hex[] = "0123456789abcdef";
    for (std::size_t at = 0; at < text.size();) {
        char32_t c;
        const auto length = decode(text, at, c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20 || c == 0x7F) {
            out += "\\u00";
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        } else if (c == 0xFFFD && length == 1) {
            out += "\\ufffd";
        } else {
            out.append(text.substr(at, length));
        }
        at += length;
    }
    out += '"';
}

std::string_view applicability_name(Applicability applicability) {
    switch (applicability) {
    case Applicability::MachineApplicable: return "MachineApplicable";
    case Applicability::MaybeIncorrect: return "MaybeIncorrect";
    case Applicability::HasPlaceholders: return "HasPlaceholders";
    case Applicability::Unspecified: return "Unspecified";
    }
    return "Unspecified";
}

void json_span(std::string& out, const Span& span) {
    out += "\"file\":";
    json_string(out, span.file);
    out += ",\"offset\":" + std::to_string(span.offset) + ",\"length\":" + std::to_string(span.length) +
           ",\"line\":" + std::to_string(span.line) + ",\"column\":" + std::to_string(span.column);
}

} // namespace

std::size_t display_width(char32_t c) {
    if (c == 0) return 0;
    if (contains(zero_width, c)) return 0;
    if (contains(wide, c)) return 2;
    return 1;
}

std::string render_human(const Diagnostic& diagnostic, const SourceMap& sources, const RenderOptions& options) {
    const Paint paint(options.color);
    const auto* data = diagnostic.data();
    const auto severity = diagnostic.severity();
    std::string out;
    std::string head(severity_word(severity));
    if (!diagnostic.code().empty()) head += "[" + std::string(diagnostic.code()) + "]";
    out += paint(head, severity_style(severity));
    out += paint(": " + std::string(diagnostic.what()), bold);
    out += '\n';

    // Labels grouped by file, the primary label's file first.
    std::vector<Section> sections;
    std::vector<DiagnosticLabel> unplaced;
    for (const auto& label : diagnostic.labels()) {
        if (label.span.file.empty()) continue;
        auto section = std::ranges::find(sections, label.span.file, &Section::name);
        if (section == sections.end()) {
            sections.push_back({label.span.file, sources.find(label.span.file), label.span, {}});
            section = sections.end() - 1;
        }
        if (!section->file) {
            if (!label.primary) unplaced.push_back(label);
            continue;
        }
        if (auto mark = place(*section->file, label)) section->marks.push_back(std::move(*mark));
        else if (!label.primary) unplaced.push_back(label);
    }
    std::vector<std::pair<const DiagnosticSuggestion*, std::optional<std::vector<SuggestionLine>>>> suggestions;
    if (data) {
        for (const auto& suggestion : data->suggestions)
            suggestions.emplace_back(&suggestion, suggestion_lines(suggestion, sources));
    }
    std::size_t widest = 1;
    for (const auto& section : sections)
        for (const auto& mark : section.marks) widest = std::max(widest, mark.line + 1);
    for (const auto& [suggestion, lines] : suggestions)
        if (lines) for (const auto& line : *lines) widest = std::max(widest, line.line + 1);
    const auto gutter = digits(widest);
    const std::string pad(gutter, ' ');
    const auto available = options.width > gutter + 3 + 20 ? options.width - gutter - 3 : 20;
    const auto bar = paint("|", blue);
    const auto empty_gutter = pad + " " + bar + "\n";

    const auto number = [&](std::size_t line) {
        auto text = std::to_string(line + 1);
        return paint(std::string(gutter - text.size(), ' ') + text, blue);
    };
    const auto source_line = [&](std::size_t line, const std::string& text) {
        return rtrim(number(line) + " " + bar + " " + text) + "\n";
    };

    bool snippet = false;
    for (std::size_t s = 0; s < sections.size(); ++s) {
        auto& section = sections[s];
        const auto arrow = s == 0 ? "-->" : ":::";
        out += pad + paint(arrow, blue) + " " + location(section.first) + "\n";
        if (!section.file || section.marks.empty()) continue;
        snippet = true;
        out += empty_gutter;
        std::ranges::stable_sort(section.marks, [](const Mark& a, const Mark& b) {
            return std::tie(a.line, b.primary, a.start) < std::tie(b.line, a.primary, b.start);
        });
        std::optional<std::size_t> previous;
        for (std::size_t m = 0; m < section.marks.size();) {
            const auto line_number = section.marks[m].line;
            auto last = m;
            while (last < section.marks.size() && section.marks[last].line == line_number) ++last;
            if (previous && line_number > *previous + 1) {
                if (line_number == *previous + 2) {
                    const auto between = cells(section.file->line_text(*previous + 1));
                    out += source_line(*previous + 1, view(between, 0, available).text);
                } else {
                    out += paint("...", blue) + "\n";
                }
            }
            const auto line = cells(section.file->line_text(line_number));
            const auto total = total_width(line);
            std::size_t first = total;
            for (auto i = m; i < last; ++i) first = std::min(first, column_of(line, section.marks[i].start));
            const auto shown = view(line, first, available);
            out += source_line(line_number, shown.text);
            for (auto i = m; i < last; ++i) {
                const auto& mark = section.marks[i];
                const auto start = shown.map(column_of(line, mark.start));
                auto stop = shown.map(mark.continues ? total : column_of(line, mark.end));
                if (stop <= start) stop = start + 1;
                const auto style = mark.primary ? severity_style(severity) : blue;
                std::string marks(stop - start, mark.primary ? '^' : '-');
                if (mark.continues) marks += "...";
                std::string row = pad + " " + bar + " " + std::string(start, ' ') + paint(marks, style);
                if (!mark.text.empty()) row += " " + paint(mark.text, style);
                out += row + "\n";
            }
            previous = line_number;
            m = last;
        }
    }
    for (const auto& label : unplaced)
        out += pad + " " + paint("=", blue) + " " + location(label.span) + ": " + label.text + "\n";

    const auto notes = all_notes(diagnostic, sources);
    const bool more = !notes.empty() || (data && (!data->helps.empty() || !data->suggestions.empty()));
    if (snippet && more) out += empty_gutter;
    for (const auto& note : notes)
        append_lines(out, pad + " " + paint("=", blue) + " " + paint("note", bold) + ": ", gutter + 9, note);
    if (data) {
        for (const auto& help : data->helps)
            append_lines(out, pad + " " + paint("=", blue) + " " + paint("help", bold) + ": ", gutter + 9, help);
    }
    for (const auto& [suggestion, lines] : suggestions) {
        out += paint("help", severity_style(Severity::Help)) + paint(": " + suggestion->message, bold) + "\n";
        if (!lines) continue;
        std::string current_file = diagnostic.span.file;
        bool opened = false;
        for (const auto& line : *lines) {
            if (line.file->name() != current_file) {
                current_file = line.file->name();
                out += pad + paint(":::", blue) + " " + current_file + ":" + std::to_string(line.line + 1) + "\n";
                opened = false;
            }
            if (!opened) out += empty_gutter;
            opened = true;
            const auto edited = cells(line.text);
            std::size_t first = total_width(edited);
            for (const auto& mark : line.marks) first = std::min(first, std::get<0>(mark));
            const auto shown = view(edited, first, available);
            out += source_line(line.line, shown.text);
            std::string row = pad + " " + bar + " ";
            std::size_t column = 0;
            for (const auto& [a, b, c] : line.marks) {
                const auto start = shown.map(a);
                auto stop = shown.map(b);
                if (stop <= start) stop = start + 1;
                if (start < column) continue;
                row += std::string(start - column, ' ');
                row += paint(std::string(stop - start, c), c == '+' ? "1;32" : c == '-' ? "1;31" : "1;33");
                column = stop;
            }
            out += row + "\n";
        }
    }
    return out;
}

std::string render_short(const Diagnostic& diagnostic) {
    std::string out = location(diagnostic.span) + ": " + std::string(severity_word(diagnostic.severity()));
    if (!diagnostic.code().empty()) out += "[" + std::string(diagnostic.code()) + "]";
    out += ": ";
    out += diagnostic.what();
    out += '\n';
    return out;
}

std::string render_json(const Diagnostic& diagnostic, const SourceMap& sources, std::size_t width) {
    const auto* data = diagnostic.data();
    std::string out = "{\"code\":";
    if (diagnostic.code().empty()) out += "null";
    else json_string(out, diagnostic.code());
    out += ",\"severity\":";
    json_string(out, severity_word(diagnostic.severity()));
    out += ",\"message\":";
    json_string(out, diagnostic.what());
    out += ",\"spans\":[";
    bool first = true;
    for (const auto& label : diagnostic.labels()) {
        if (!first) out += ',';
        first = false;
        out += '{';
        json_span(out, label.span);
        out += ",\"primary\":";
        out += label.primary ? "true" : "false";
        out += ",\"label\":";
        if (label.text.empty()) out += "null";
        else json_string(out, label.text);
        out += '}';
    }
    out += "],\"notes\":[";
    first = true;
    for (const auto& note : all_notes(diagnostic, sources)) {
        if (!first) out += ',';
        first = false;
        json_string(out, note);
    }
    out += "],\"help\":[";
    first = true;
    if (data) {
        for (const auto& help : data->helps) {
            if (!first) out += ',';
            first = false;
            json_string(out, help);
        }
    }
    out += "],\"suggestions\":[";
    first = true;
    if (data) {
        for (const auto& suggestion : data->suggestions) {
            if (!first) out += ',';
            first = false;
            out += "{\"message\":";
            json_string(out, suggestion.message);
            out += ",\"applicability\":";
            json_string(out, applicability_name(suggestion.applicability));
            out += ",\"edits\":[";
            bool first_edit = true;
            for (const auto& edit : suggestion.edits) {
                if (!first_edit) out += ',';
                first_edit = false;
                out += '{';
                json_span(out, edit.span);
                out += ",\"replacement\":";
                json_string(out, edit.replacement);
                out += '}';
            }
            out += "]}";
        }
    }
    out += "],\"args\":{";
    first = true;
    if (data) {
        for (const auto& [key, value] : data->args) {
            if (!first) out += ',';
            first = false;
            json_string(out, key);
            out += ':';
            json_string(out, value);
        }
    }
    out += "},\"rendered\":";
    json_string(out, render_human(diagnostic, sources, {false, width}));
    out += "}\n";
    return out;
}

std::size_t apply_edits(std::string& text, std::vector<DiagnosticEdit> edits) {
    std::ranges::stable_sort(edits, [](const DiagnosticEdit& a, const DiagnosticEdit& b) {
        return std::tie(b.span.offset, b.span.length, b.replacement) < std::tie(a.span.offset, a.span.length, a.replacement);
    });
    // The same edit suggested twice applies once.
    const auto duplicates = std::ranges::unique(edits, [](const DiagnosticEdit& a, const DiagnosticEdit& b) {
        return a.span.offset == b.span.offset && a.span.length == b.span.length && a.replacement == b.replacement;
    });
    edits.erase(duplicates.begin(), duplicates.end());
    std::size_t applied = 0;
    auto limit = text.size();
    for (const auto& edit : edits) {
        const auto start = edit.span.offset;
        if (start > limit || edit.span.length > limit - start) continue;
        text.replace(start, edit.span.length, edit.replacement);
        limit = start;
        ++applied;
    }
    return applied;
}

DiagnosticPrinter::DiagnosticPrinter(DiagnosticConfig config, RenderOptions options, const SourceMap& sources)
    : config_(std::move(config)), options_(options), sources_(sources) {}

bool DiagnosticPrinter::enabled(const Diagnostic& warning) const {
    if (const auto found = config_.warnings.find(warning.name()); found != config_.warnings.end() && !found->second)
        return false;
    if (!config_.library_warnings) {
        if (const auto file = sources_.find(warning.span.file); file && file->kind() == SourceKind::Library)
            return false;
    }
    return true;
}

std::size_t DiagnosticPrinter::warnings(std::ostream& out, const std::vector<Diagnostic>& warnings) {
    std::vector<const Diagnostic*> shown;
    for (const auto& warning : warnings)
        if (enabled(warning)) shown.push_back(&warning);
    std::ranges::stable_sort(shown, [](const Diagnostic* a, const Diagnostic* b) {
        return std::tie(a->span.file, a->span.offset) < std::tie(b->span.file, b->span.offset);
    });
    for (const auto* warning : shown) {
        if (!config_.warnings_are_errors) {
            print(out, *warning);
            continue;
        }
        auto error = *warning;
        error.edit().severity = Severity::Error;
        error.note("-Werror makes every warning an error");
        print(out, error);
    }
    return shown.size();
}

void DiagnosticPrinter::print(std::ostream& out, const Diagnostic& diagnostic) {
    switch (config_.format) {
    case ErrorFormat::Short:
        out << render_short(diagnostic);
        break;
    case ErrorFormat::Json:
        out << render_json(diagnostic, sources_, options_.width);
        break;
    case ErrorFormat::Human:
        // A blank line separates diagnostics, as rustc separates them.
        if (!first_) out << '\n';
        out << render_human(diagnostic, sources_, options_);
        break;
    }
    first_ = false;
}

} // namespace neura::cli
