#pragma once

#include "util/diagnostic.hpp"
#include "util/source_map.hpp"

#include <cstddef>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace neura::cli {

// How the command line prints diagnostics (Tooling.md, Diagnostics).
enum class ErrorFormat { Human, Short, Json };
enum class ColorChoice { Auto, Always, Never };

struct RenderOptions {
    bool color = false;
    // Terminal columns; longer source lines are trimmed around their labels.
    std::size_t width = 100;
};

// The display width of code point `c`: 0 for combining marks, 2 for East
// Asian wide and fullwidth characters, otherwise 1. A fixed table, so that
// output does not depend on the locale.
std::size_t display_width(char32_t c);

// The rustc-style layout: header, location arrow, numbered source lines with
// ^ under the primary span and - under secondary spans, notes, and help
// blocks that show suggested edits. Ends with a newline.
std::string render_human(const Diagnostic& diagnostic, const SourceMap& sources, const RenderOptions& options);
// One line, `file:L:C: error: message` or `file:L:C: error[CODE]: message`,
// with a newline.
std::string render_short(const Diagnostic& diagnostic);
// One JSON object on one line, with a newline. `rendered` is the human form
// without color.
std::string render_json(const Diagnostic& diagnostic, const SourceMap& sources, std::size_t width);

// Applies `edits` (byte offsets into `text`) from back to front. An edit that
// overlaps one already applied or lies outside the text is skipped. Returns
// the number applied.
std::size_t apply_edits(std::string& text, std::vector<DiagnosticEdit> edits);

// The diagnostic options of a compiling command.
struct DiagnosticConfig {
    ErrorFormat format = ErrorFormat::Human;
    ColorChoice color = ColorChoice::Auto;
    // -W<name> (true) and -Wno-<name> (false); the last one for a name wins.
    std::map<std::string, bool, std::less<>> warnings;
    bool warnings_are_errors = false;
    bool library_warnings = false;
};

// Prints diagnostics in the configured format.
class DiagnosticPrinter {
public:
    DiagnosticPrinter(DiagnosticConfig config, RenderOptions options, const SourceMap& sources);

    // Prints the warnings that the configuration enables, sorted by file and
    // offset, and returns how many were printed. Under -Werror they print as
    // errors.
    std::size_t warnings(std::ostream& out, const std::vector<Diagnostic>& warnings);
    void print(std::ostream& out, const Diagnostic& diagnostic);
    // Ends the human format's output: after errors with codes, the line that
    // points to `neura explain`, once per run as rustc prints it. Prints
    // nothing in the short and JSON formats.
    void finish(std::ostream& out);
    const DiagnosticConfig& config() const { return config_; }

private:
    bool enabled(const Diagnostic& warning) const;

    DiagnosticConfig config_;
    RenderOptions options_;
    const SourceMap& sources_;
    bool first_ = true;
    // The codes of the errors printed, in order of first appearance.
    std::vector<std::string> codes_;
};

} // namespace neura::cli
